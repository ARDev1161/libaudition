#pragma once

#include <memory>

#include <audition/backends/world/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/voice.hpp>

namespace audition {

class AUDITION_API WorldVoiceTraitsEstimator final
    : public IVoiceTraitsEstimator {
public:
    explicit WorldVoiceTraitsEstimator(
        WorldVoiceTraitsOptions options = {});
    ~WorldVoiceTraitsEstimator() override;

    WorldVoiceTraitsEstimator(
        const WorldVoiceTraitsEstimator&) = delete;
    WorldVoiceTraitsEstimator& operator=(
        const WorldVoiceTraitsEstimator&) = delete;
    WorldVoiceTraitsEstimator(
        WorldVoiceTraitsEstimator&&) noexcept;
    WorldVoiceTraitsEstimator& operator=(
        WorldVoiceTraitsEstimator&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] VoiceTraitsCapabilities capabilities() const override;
    [[nodiscard]] VoiceTraits estimate(
        AudioView speech) const override;

    [[nodiscard]] const WorldVoiceTraitsOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
