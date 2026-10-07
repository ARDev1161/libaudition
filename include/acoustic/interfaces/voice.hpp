#pragma once

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/backend/capabilities.hpp>
#include <acoustic/voice/types.hpp>

namespace acoustic {

class IVoiceTraitsEstimator {
public:
    virtual ~IVoiceTraitsEstimator() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual VoiceTraits estimate(AudioView speech) const = 0;
};

class IVoiceStateEstimator {
public:
    virtual ~IVoiceStateEstimator() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual VoiceState estimate(AudioView speech) const = 0;
};

}  // namespace acoustic
