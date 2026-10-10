#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include <audition/core/export.hpp>
#include <audition/classify/types.hpp>

namespace audition {

// EfficientAT MobileNet/DyMN ONNX adapter for *preprocessed* mel tensors.
// This intentionally does not implement IAudioClassifier until the
// reference Kaldi-compatible waveform frontend has been validated.
// Supported exports must expose a float32 [1,1,128,T] log-mel input and
// a float32 [1,527] (or [527]) LOGITS output; sigmoid is applied here.
struct EfficientAtOnnxOptions {
    std::filesystem::path model{};
    std::filesystem::path labels{};
    std::size_t top_k{5U};
    int intra_op_threads{1};
};

class AUDITION_API EfficientAtSpectrogramTagger final {
public:
    explicit EfficientAtSpectrogramTagger(EfficientAtOnnxOptions options);
    ~EfficientAtSpectrogramTagger();
    EfficientAtSpectrogramTagger(EfficientAtSpectrogramTagger&&) noexcept;
    EfficientAtSpectrogramTagger& operator=(EfficientAtSpectrogramTagger&&) noexcept;
    EfficientAtSpectrogramTagger(const EfficientAtSpectrogramTagger&) = delete;
    EfficientAtSpectrogramTagger& operator=(const EfficientAtSpectrogramTagger&) = delete;

    [[nodiscard]] ClassificationResult classifyLogMel(
        const float* normalized_mel, std::size_t mel_bins,
        std::size_t frames) const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audition
