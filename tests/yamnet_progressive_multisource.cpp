#include <audition/backends/yamnet/audio_tagger.hpp>
#include <audition/classify/progressive_source_classification_runtime.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

unsigned boundedInt(const char* arg, unsigned min, unsigned max) {
    const std::string value{arg};
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        })) {
        throw std::invalid_argument{"Invalid integer argument"};
    }
    const auto n = std::stoul(value);
    if (n < min || n > max) throw std::invalid_argument{"Out of range"};
    return static_cast<unsigned>(n);
}

struct Totals {
    std::uint64_t drops{0};
    std::uint64_t completed{0};
    double max_inference_ms{0.0};
    double max_queue_wait_ms{0.0};
    double total_inference_ms{0.0};
    double total_queue_wait_ms{0.0};
};

void print(const Totals& t) {
    std::cout << "{\"dropped_windows\":" << t.drops
              << ",\"completed_windows\":" << t.completed
              << ",\"max_inference_ms\":" << t.max_inference_ms
              << ",\"max_queue_wait_ms\":" << t.max_queue_wait_ms
              << ",\"mean_inference_ms\":" << (t.completed == 0U ? 0.0
                  : t.total_inference_ms / static_cast<double>(t.completed))
              << ",\"mean_queue_wait_ms\":" << (t.completed == 0U ? 0.0
                  : t.total_queue_wait_ms / static_cast<double>(t.completed)) << "}";
}

void run(const std::string& model, const std::string& labels,
         unsigned source_count, unsigned duration_seconds, bool real_time) {
    audition::YamnetOnnxOptions options{};
    options.model = model;
    options.labels = labels;
    options.top_k = 5U;
    auto quick = std::make_unique<audition::YamnetAudioTagger>(options);
    auto refined = std::make_unique<audition::YamnetAudioTagger>(options);
    if (!quick->supportsWindowSamples(15600U) ||
        !refined->supportsWindowSamples(80000U)) {
        throw std::runtime_error{"The pinned waveform ONNX must support 1s and 5s"};
    }
    std::vector<audition::ProgressiveClassificationStage> stages;
    stages.push_back({"quick", std::move(quick), 15600U, 7800U});
    stages.push_back({"refined", std::move(refined), 80000U, 16000U});
    audition::ProgressiveSourceClassificationRuntime classifier{
        16000U, std::move(stages), source_count};

    constexpr std::size_t hop = 160U; // 10ms at 16 kHz
    constexpr double pi = 3.14159265358979323846;
    const auto total_samples = static_cast<std::size_t>(duration_seconds) * 16000U;
    std::vector<float> chunk(hop);
    double worst_push_ms = 0.0;
    const auto start = Clock::now();
    for (std::size_t offset = 0; offset < total_samples; offset += hop) {
        // Mimic incoming ODAS 10ms batches; users can also stress a burst input.
        if (real_time) {
            const auto next = start + std::chrono::microseconds{static_cast<std::int64_t>(
                (offset / hop) * 10000U)};
            std::this_thread::sleep_until(next);
        }
        for (unsigned source = 0; source < source_count; ++source) {
            const auto hz = 250.0 + 37.0 * static_cast<double>(source);
            for (std::size_t i = 0; i < hop; ++i) {
                const auto index = offset + i;
                chunk[i] = static_cast<float>(
                    0.10 * std::sin(2.0 * pi * hz * static_cast<double>(index) / 16000.0));
            }
            const auto begun = Clock::now();
            classifier.push(source, chunk.data(), chunk.size());
            const auto elapsed = std::chrono::duration<double, std::milli>(
                Clock::now() - begun).count();
            worst_push_ms = std::max(worst_push_ms, elapsed);
        }
    }
    const auto feed_ms = std::chrono::duration<double, std::milli>(
        Clock::now() - start).count();

    const auto short_end = (total_samples - 15600U) / 7800U * 7800U + 15600U;
    const auto expected_end = static_cast<std::uint64_t>(total_samples);
    const auto wait_until = Clock::now() + std::chrono::seconds{25};
    bool caught_up = false;
    while (Clock::now() < wait_until) {
        caught_up = true;
        for (unsigned source = 0; source < source_count; ++source) {
            const auto st = classifier.status(source);
            if (st.stages.size() != 2U) throw std::runtime_error{"Wrong stage count"};
            if (st.stages[0].status.state == audition::SourceClassificationState::Error ||
                st.stages[1].status.state == audition::SourceClassificationState::Error) {
                throw std::runtime_error{"YAMNet source stage failed"};
            }
            const auto q = st.stages[0].status.result_end_sample;
            const auto r = st.stages[1].status.result_end_sample;
            if (!q || *q != short_end || !r || *r != expected_end) {
                caught_up = false;
            }
        }
        if (caught_up) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!caught_up) {
        throw std::runtime_error{"Timed out waiting for the newest YAMNet windows"};
    }
    Totals q{};
    Totals r{};
    for (unsigned source = 0; source < source_count; ++source) {
        const auto st = classifier.status(source);
        for (std::size_t index = 0; index < 2U; ++index) {
            const auto& state = st.stages[index].status;
            if (!state.max_inference_ms || !state.max_queue_wait_ms ||
                !state.mean_inference_ms || !state.mean_queue_wait_ms ||
                !std::isfinite(*state.max_inference_ms) ||
                !std::isfinite(*state.max_queue_wait_ms)) {
                throw std::runtime_error{"Invalid YAMNet runtime timing metrics"};
            }
            auto& totals = index == 0U ? q : r;
            totals.drops += state.dropped_windows;
            totals.completed += state.completed_windows;
            totals.max_inference_ms = std::max(totals.max_inference_ms,
                                               *state.max_inference_ms);
            totals.max_queue_wait_ms = std::max(totals.max_queue_wait_ms,
                                                *state.max_queue_wait_ms);
            totals.total_inference_ms += *state.mean_inference_ms *
                                         static_cast<double>(state.completed_windows);
            totals.total_queue_wait_ms += *state.mean_queue_wait_ms *
                                          static_cast<double>(state.completed_windows);
        }
    }
    const auto full_wall_ms = std::chrono::duration<double, std::milli>(
        Clock::now() - start).count();
    std::cout << std::fixed << std::setprecision(3)
              << "{\"model\":\"real_yamnet_onnx\",\"sources\":" << source_count
              << ",\"seconds_per_source\":" << duration_seconds
              << ",\"real_time_input\":" << (real_time ? "true" : "false")
              << ",\"feed_wall_ms\":" << feed_ms
              << ",\"total_wall_ms\":" << full_wall_ms
              << ",\"worst_push_ms\":" << worst_push_ms
              << ",\"quick\":";
    print(q);
    std::cout << ",\"refined\":";
    print(r);
    std::cout << "}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 6) {
            std::cerr << "usage: yamnet_progressive_multisource MODEL LABELS SOURCES(1..16) "
                         "SECONDS(5..15) real-time|burst\n";
            return 2;
        }
        const auto sources = boundedInt(argv[3], 1U, 16U);
        const auto seconds = boundedInt(argv[4], 5U, 15U);
        const std::string mode{argv[5]};
        if (mode != "real-time" && mode != "burst") {
            throw std::invalid_argument{"Mode must be real-time or burst"};
        }
        run(argv[1], argv[2], sources, seconds, mode == "real-time");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: YAMNet multi-source benchmark: " << e.what() << '\n';
        return 1;
    }
}
