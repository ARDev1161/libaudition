#include <audition/backends/efficientat/audio_tagger.hpp>
#include <audition/audio/audio_buffer.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: efficientat_real_model_smoke MODEL.onnx LABELS.txt\n";
            return 2;
        }
        audition::EfficientAtOnnxOptions cfg{};
        cfg.model = argv[1];
        cfg.labels = argv[2];
        cfg.top_k = 5;
        audition::EfficientAtAudioTagger tagger{cfg};
        std::vector<float> signal(std::string(argv[1]).find("mn10_as.onnx") != std::string::npos ? 160000U : 32000U);
        for (std::size_t i = 0; i < signal.size(); ++i) {
            signal[i] = 0.12F * static_cast<float>(
                std::sin(2.0 * 3.141592653589793 * 440.0 *
                         static_cast<double>(i) / 32000.0));
        }
        audition::AudioBuffer buffer{std::move(signal),
            {32000U, 1U, audition::AudioLayout::Interleaved},
            audition::Timestamp{}, 0U};
        const auto out = tagger.classify(buffer.view());
        if (out.classes.size() != 5U) throw std::runtime_error{"Expected five class scores"};
        for (const auto& cls : out.classes) {
            if (cls.label.empty() || !std::isfinite(cls.probability.value())) {
                throw std::runtime_error{"Invalid real EfficientAT class score"};
            }
            std::cout << cls.label << ": " << cls.probability.value() << '\n';
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EfficientAT smoke failed: " << ex.what() << '\n';
        return 1;
    }
}
