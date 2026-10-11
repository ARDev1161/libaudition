#include <audition/backends/yamnet/audio_tagger.hpp>
#include <audition/classify/progressive_source_classification_runtime.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: yamnet_progressive_smoke MODEL.onnx LABELS.csv\n";
            return 2;
        }
        audition::YamnetOnnxOptions options{};
        options.model = argv[1];
        options.labels = argv[2];
        options.top_k = 5U;
        auto quick = std::make_unique<audition::YamnetAudioTagger>(options);
        auto refined = std::make_unique<audition::YamnetAudioTagger>(options);
        if (!quick->supportsWindowSamples(15600U) ||
            !refined->supportsWindowSamples(80000U)) {
            throw std::runtime_error{"Pinned real ONNX model must support both 0.975s and 5s"};
        }
        std::vector<audition::ProgressiveClassificationStage> stages;
        stages.push_back({"quick", std::move(quick), 15600U, 7800U});
        stages.push_back({"refined", std::move(refined), 80000U, 16000U});
        audition::ProgressiveSourceClassificationRuntime runtime{
            16000U, std::move(stages), 4U};

        constexpr double pi = 3.14159265358979323846;
        std::vector<float> pcm(80000U);
        for (std::size_t i = 0U; i < pcm.size(); ++i) {
            pcm[i] = static_cast<float>(
                0.25 * std::sin(2.0 * pi * 440.0 * static_cast<double>(i) / 16000.0));
        }
        runtime.push(7U, pcm.data(), 15600U);
        bool quick_ready = false;
        for (int i = 0; i < 3000; ++i) {
            const auto state = runtime.status(7U);
            if (state.selected_stage == 0U && state.result.has_value() &&
                state.result->classes.size() == 5U &&
                !state.stages[1].status.result.has_value()) {
                quick_ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        if (!quick_ready) {
            throw std::runtime_error{"Quick real-weight YAMNet prediction not available"};
        }

        runtime.push(7U, pcm.data() + 15600U, pcm.size() - 15600U);
        bool refined_ready = false;
        for (int i = 0; i < 5000; ++i) {
            const auto state = runtime.status(7U);
            if (state.selected_stage == 1U && state.result.has_value() &&
                state.result->classes.size() == 5U &&
                state.stages[0].status.result.has_value() &&
                state.stages[1].name == "refined") {
                refined_ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        if (!refined_ready) {
            const auto state = runtime.status(7U);
            throw std::runtime_error{"Five-second refinement absent; stage error: " +
                state.stages[1].status.error};
        }
        runtime.forget(7U);
        const auto forgotten = runtime.status(7U);
        if (forgotten.selected_stage.has_value() ||
            forgotten.result.has_value()) {
            throw std::runtime_error{"Forgotten source retained old prediction"};
        }
        std::cout << "PASS: real YAMNet fast -> 5s refined -> forgotten source\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "YAMNet progressive smoke failed: " << error.what() << '\n';
        return 1;
    }
}
