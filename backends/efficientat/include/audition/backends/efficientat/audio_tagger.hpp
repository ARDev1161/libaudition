#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include <audition/core/export.hpp>
#include <audition/classify/types.hpp>
#include <audition/interfaces/classify.hpp>
#include <audition/backends/efficientat/frontend.hpp>

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

    [[nodiscard]] std::size_t fixedInputFrames() const noexcept;
    [[nodiscard]] ClassificationResult classifyLogMel(
        const float* normalized_mel, std::size_t mel_bins,
        std::size_t frames) const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// End-to-end mono 32 kHz classifier. It uses the reference-oriented
// EfficientAT frontend and delegates ONNX logits inference to the
// spectrogram adapter above.
class AUDITION_API EfficientAtAudioTagger final : public IAudioClassifier {
public:
    explicit EfficientAtAudioTagger(EfficientAtOnnxOptions options);
    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] ClassifierCapabilities capabilities() const override;
    [[nodiscard]] ClassificationResult classify(AudioView audio) const override;
private:
    EfficientAtWaveformFrontend frontend_{};
    EfficientAtSpectrogramTagger model_;
};

// Explicit 16 kHz adapter for separated ODAS tracks. The upsampler is
// part of the backend, not hidden in the generic inference runtime.
class AUDITION_API EfficientAt16kAudioTagger final : public IAudioClassifier {
public:
    explicit EfficientAt16kAudioTagger(EfficientAtOnnxOptions options);
    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] ClassifierCapabilities capabilities() const override;
    [[nodiscard]] ClassificationResult classify(AudioView audio) const override;
private:
    EfficientAtAudioTagger classifier_;
};

} // namespace audition
