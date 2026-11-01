#pragma once

#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>
#include <memory>
#include <string>

namespace audition {

struct WhisperCppAsrOptions {
    std::string model_path{};
    std::string language{"ru"}; // "auto" uses whisper language detection
    int threads{4};
    bool use_gpu{false};
};

class AUDITION_API WhisperCppAsr final : public IAsrEngine {
public:
    explicit WhisperCppAsr(WhisperCppAsrOptions options);
    ~WhisperCppAsr() override;
    WhisperCppAsr(const WhisperCppAsr&) = delete;
    WhisperCppAsr& operator=(const WhisperCppAsr&) = delete;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AsrCapabilities capabilities() const override;
    [[nodiscard]] Transcript transcribe(const SpeechSegment& segment) const override;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audition
