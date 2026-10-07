#pragma once

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/authenticity/types.hpp>
#include <acoustic/backend/capabilities.hpp>

namespace acoustic {

class IAudioAuthenticityDetector {
public:
    virtual ~IAudioAuthenticityDetector() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AuthenticityResult analyze(AudioView speech) const = 0;
};

}  // namespace acoustic
