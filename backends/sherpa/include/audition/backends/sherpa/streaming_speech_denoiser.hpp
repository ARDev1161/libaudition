#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/audio.hpp>

namespace audition {

class AUDITION_API SherpaStreamingSpeechDenoiser final : public INoiseSuppressor {
public:
    explicit SherpaStreamingSpeechDenoiser(SherpaSpeechDenoiserOptions options);
    ~SherpaStreamingSpeechDenoiser() override;

    SherpaStreamingSpeechDenoiser(const SherpaStreamingSpeechDenoiser&) = delete;
    SherpaStreamingSpeechDenoiser& operator=(const SherpaStreamingSpeechDenoiser&) = delete;
    SherpaStreamingSpeechDenoiser(SherpaStreamingSpeechDenoiser&&) noexcept;
    SherpaStreamingSpeechDenoiser& operator=(SherpaStreamingSpeechDenoiser&&) noexcept;

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
