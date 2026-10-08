#pragma once

#include <audition/audio/audio_buffer.hpp>
#include <audition/backend/capabilities.hpp>
#include <audition/voice/types.hpp>

namespace audition {

class IVoiceTraitsEstimator {
public:
    virtual ~IVoiceTraitsEstimator() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual VoiceTraitsCapabilities capabilities() const = 0;
    [[nodiscard]] virtual VoiceTraits estimate(AudioView speech) const = 0;
};

class IVoiceAcousticAnalyzer {
public:
    virtual ~IVoiceAcousticAnalyzer() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual VoiceAcousticCapabilities capabilities() const = 0;
    [[nodiscard]] virtual VoiceAcousticFeatures analyze(AudioView speech) const = 0;
};

class IVoiceStateEstimator {
public:
    virtual ~IVoiceStateEstimator() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual VoiceState estimate(AudioView speech) const = 0;
};

}  // namespace audition
