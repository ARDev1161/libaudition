#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include <audition/core/export.hpp>
#include <audition/interfaces/classify.hpp>

namespace audition {

// Accepts waveform-input YAMNet ONNX exports, not spectrogram-input variants.
// The exported scores output must have 521 AudioSet class columns.
struct YamnetOnnxOptions {
    std::filesystem::path model{};
    std::filesystem::path labels{};
    std::int32_t intra_op_threads{1};
    std::size_t top_k{3};
};

class AUDITION_API YamnetAudioTagger final : public IAudioClassifier {
public:
    explicit YamnetAudioTagger(YamnetOnnxOptions options);
    ~YamnetAudioTagger() override;

    YamnetAudioTagger(const YamnetAudioTagger&) = delete;
    YamnetAudioTagger& operator=(const YamnetAudioTagger&) = delete;
    YamnetAudioTagger(YamnetAudioTagger&&) noexcept;
    YamnetAudioTagger& operator=(YamnetAudioTagger&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] ClassifierCapabilities capabilities() const override;
    [[nodiscard]] ClassificationResult classify(AudioView audio) const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
