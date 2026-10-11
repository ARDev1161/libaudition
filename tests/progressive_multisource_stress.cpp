#include <audition/classify/progressive_source_classification_runtime.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

class SimulatedClassifier final : public audition::IAudioClassifier {
public:
    SimulatedClassifier(std::string label, std::size_t expected_samples, unsigned delay_ms)
        : label_(std::move(label)), expected_samples_(expected_samples),
          delay_ms_(delay_ms) {}

    audition::BackendInfo backendInfo() const override {
        return {"simulated-" + label_, "1"};
    }
    audition::ClassifierCapabilities capabilities() const override {
        audition::ClassifierCapabilities caps{};
        caps.audio.supported_sample_rates_hz = {16000U};
        caps.audio.preferred_frame_count = expected_samples_;
        return caps;
    }
    audition::ClassificationResult classify(audition::AudioView audio) const override {
        if (audio.sampleCount() != expected_samples_) {
            throw std::runtime_error{"Unexpected synthetic stage window size"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms_});
        audition::ClassificationResult result{};
        result.classes.push_back({label_, audition::Probability::from(0.9)});
        return result;
    }

private:
    std::string label_{};
    std::size_t expected_samples_{};
    unsigned delay_ms_{};
};

int positiveInt(const char* text, int min_value, int max_value) {
    const std::string digits{text};
    if (digits.empty() ||
        !std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        })) {
        throw std::invalid_argument{"Expected positive integer arguments"};
    }
    const long parsed = std::stol(digits);
    if (parsed < min_value || parsed > max_value) {
        throw std::out_of_range{"Argument is outside supported test bounds"};
    }
    return static_cast<int>(parsed);
}

struct Stats {
    std::uint64_t drops{0};
    std::uint64_t completions{0};
    std::uint64_t min_end_sample{std::numeric_limits<std::uint64_t>::max()};
    double max_queue_wait_ms{0.0};
    double max_inference_ms{0.0};
};

void printStage(const Stats& info) {
    std::cout << "{\"drops\":" << info.drops
              << ",\"completed\":" << info.completions
              << ",\"min_result_end_sample\":" << info.min_end_sample
              << ",\"max_queue_wait_ms\":" << info.max_queue_wait_ms
              << ",\"max_inference_ms\":" << info.max_inference_ms << "}";
}

void benchmark(unsigned sources, unsigned seconds,
               unsigned quick_delay_ms, unsigned refined_delay_ms) {
    constexpr std::uint32_t rate = 16000U;
    constexpr std::size_t hop = 160U; // 10 ms ODAS capture chunks
    constexpr std::size_t quick_window = 15600U;
    constexpr std::size_t refined_window = 80000U;
    if (seconds < 5U) {
        throw std::invalid_argument{"At least five seconds needed for refinement"};
    }
    std::vector<audition::ProgressiveClassificationStage> stages;
    stages.push_back({"quick", std::make_unique<SimulatedClassifier>(
                         "quick", quick_window, quick_delay_ms),
                     quick_window, 7800U});
    stages.push_back({"refined", std::make_unique<SimulatedClassifier>(
                         "refined", refined_window, refined_delay_ms),
                     refined_window, 16000U});
    audition::ProgressiveSourceClassificationRuntime runtime{
        rate, std::move(stages), sources};
    const auto expected_samples = seconds * static_cast<std::size_t>(rate);
    std::vector<float> chunk(hop, 0.0F);
    double worst_push_ms = 0.0;
    const auto feed_start = Clock::now();
    for (std::size_t frame = 0; frame < expected_samples / hop; ++frame) {
        for (unsigned source = 0; source < sources; ++source) {
            const auto base = 0.01F * static_cast<float>(source + 1U);
            for (std::size_t sample = 0; sample < hop; ++sample) {
                chunk[sample] = base + static_cast<float>(sample) * 0.0001F;
            }
            const auto before = Clock::now();
            runtime.push(1000U + source, chunk.data(), chunk.size());
            const auto elapsed = std::chrono::duration<double, std::milli>(
                Clock::now() - before).count();
            worst_push_ms = std::max(worst_push_ms, elapsed);
        }
    }
    const auto feeding_ms = std::chrono::duration<double, std::milli>(
        Clock::now() - feed_start).count();

    // Wait for the last submitted context windows, NOT just first results.
    // This also checks that bounded queue coalescing lets every source catch up.
    const auto deadline = Clock::now() + std::chrono::seconds{20};
    bool caught_up = false;
    while (Clock::now() < deadline) {
        caught_up = true;
        for (unsigned source = 0; source < sources; ++source) {
            const auto snapshot = runtime.status(1000U + source);
            if (snapshot.stages.size() != 2U) {
                throw std::runtime_error{"Wrong stage count"};
            }
            for (std::size_t stage = 0; stage < snapshot.stages.size(); ++stage) {
                const auto& state = snapshot.stages[stage].status;
                // The short window's final position is aligned to its hop.
                const auto floor_end = stage == 0U
                    ? (expected_samples - quick_window) / 7800U * 7800U + quick_window
                    : expected_samples; // 5s + N*1s lands exactly on boundary
                if (!state.result_end_sample.has_value() ||
                    *state.result_end_sample != floor_end ||
                    state.completed_windows == 0U) {
                    caught_up = false;
                }
                if (state.state == audition::SourceClassificationState::Error) {
                    throw std::runtime_error{"Classifier failed: " + state.error};
                }
            }
        }
        if (caught_up) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    if (!caught_up) {
        throw std::runtime_error{"At least one source failed to reach newest audio window"};
    }

    Stats quick{};
    Stats refined{};
    for (unsigned source = 0; source < sources; ++source) {
        const auto snapshot = runtime.status(1000U + source);
        for (std::size_t stage = 0; stage < snapshot.stages.size(); ++stage) {
            const auto& status = snapshot.stages[stage].status;
            auto& stats = stage == 0U ? quick : refined;
            if (!status.last_inference_ms || !status.last_queue_wait_ms ||
                !std::isfinite(*status.last_inference_ms) ||
                !std::isfinite(*status.last_queue_wait_ms)) {
                throw std::runtime_error{"Missing timing diagnostics"};
            }
            stats.drops += status.dropped_windows;
            stats.completions += status.completed_windows;
            stats.min_end_sample = std::min(stats.min_end_sample, *status.result_end_sample);
            stats.max_inference_ms = std::max(stats.max_inference_ms,
                                               *status.last_inference_ms);
            stats.max_queue_wait_ms = std::max(stats.max_queue_wait_ms,
                                                *status.last_queue_wait_ms);
        }
    }
    // Model costs are intentionally injected and cause scheduler pressure when
    // the synthetic audio is delivered faster than real time.
    std::cout << std::fixed << std::setprecision(3)
              << "{\"sources\":" << sources << ",\"seconds_per_source\":" << seconds
              << ",\"simulated\":true,\"feed_wall_ms\":" << feeding_ms
              << ",\"worst_push_ms\":" << worst_push_ms
              << ",\"quick\":";
    printStage(quick);
    std::cout << ",\"refined\":";
    printStage(refined);
    std::cout << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 5) {
            std::cerr << "usage: progressive_multisource_stress SOURCES(1..16) "
                         "SECONDS(5..30) [QUICK_DELAY_MS(0..100) REFINED_DELAY_MS(0..100)]\n";
            return 2;
        }
        const unsigned sources = static_cast<unsigned>(positiveInt(argv[1], 1, 16));
        const unsigned seconds = static_cast<unsigned>(positiveInt(argv[2], 5, 30));
        const unsigned quick_delay = argc == 5
            ? static_cast<unsigned>(positiveInt(argv[3], 0, 100)) : 4U;
        const unsigned refined_delay = argc == 5
            ? static_cast<unsigned>(positiveInt(argv[4], 0, 100)) : 12U;
        benchmark(sources, seconds, quick_delay, refined_delay);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: progressive multi-source stress: " << error.what() << '\n';
        return 1;
    }
}
