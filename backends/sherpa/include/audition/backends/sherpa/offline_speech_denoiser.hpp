#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/audio.hpp>

namespace audition {

class AUDITION_API SherpaOfflineSpeechDenoiser final : public INoiseSuppressor {
public:
    explicit SherpaOfflineSpeechDenoiser(SherpaSpeechDenoiserOptions options);
    ~SherpaOfflineSpeechDenoiser() override;

    SherpaOfflineSpeechDenoiser(const SherpaOfflineSpeechDenoiser&) = delete;
    SherpaOfflineSpeechDenoiser& operator=(const SherpaOfflineSpeechDenoiser&) = delete;
    SherpaOfflineSpeechDenoiser(SherpaOfflineSpeechDenoiser&&) noexcept;
    SherpaOfflineSpeechDenoiser& operator=(SherpaOfflineSpeechDenoiser&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] NoiseSuppressorCapabilities capabilities() const override;
    [[nodiscard]] std::unique_ptr<INoiseSuppressorSession> createSession(
        const AudioFormat& format) const override;

    [[nodiscard]] const SherpaSpeechDenoiserOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
