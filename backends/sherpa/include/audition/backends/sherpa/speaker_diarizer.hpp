#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speaker.hpp>

namespace audition {

class AUDITION_API SherpaSpeakerDiarizer final : public ISpeakerDiarizer {
public:
    explicit SherpaSpeakerDiarizer(SherpaSpeakerDiarizationOptions options);
    ~SherpaSpeakerDiarizer() override;

    SherpaSpeakerDiarizer(const SherpaSpeakerDiarizer&) = delete;
    SherpaSpeakerDiarizer& operator=(const SherpaSpeakerDiarizer&) = delete;
    SherpaSpeakerDiarizer(SherpaSpeakerDiarizer&&) noexcept;
    SherpaSpeakerDiarizer& operator=(SherpaSpeakerDiarizer&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AudioRequirements audioRequirements() const override;
    [[nodiscard]] SpeakerDiarizationResult diarize(AudioView audio) const override;

    [[nodiscard]] const SherpaSpeakerDiarizationOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
