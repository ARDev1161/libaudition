#pragma once

#include <audition/audio/audio_buffer.hpp>
#include <audition/authenticity/types.hpp>
#include <audition/backend/capabilities.hpp>

namespace audition {

class IAudioAuthenticityDetector {
public:
    virtual ~IAudioAuthenticityDetector() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AuthenticityCapabilities capabilities() const = 0;
    [[nodiscard]] virtual AuthenticityResult analyze(AudioView speech) const = 0;
};

}  // namespace audition
