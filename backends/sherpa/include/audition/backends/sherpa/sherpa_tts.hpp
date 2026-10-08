#pragma once

#if defined(LIBAUDITION_SHERPA_TTS_ENABLED)

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/tts.hpp>

namespace audition {

class AUDITION_API SherpaTts final : public ISpeechSynthesizer {
public:
    explicit SherpaTts(SherpaTtsOptions options);
    ~SherpaTts() override;

    SherpaTts(const SherpaTts&) = delete;
    SherpaTts& operator=(const SherpaTts&) = delete;
    SherpaTts(SherpaTts&&) noexcept;
    SherpaTts& operator=(SherpaTts&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] TtsCapabilities capabilities() const override;
    [[nodiscard]] AudioBuffer synthesize(
        const SpeechSynthesisRequest& request) const override;

    [[nodiscard]] const SherpaTtsOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition

#endif
