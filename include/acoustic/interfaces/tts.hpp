#pragma once

#include <optional>
#include <string>

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/backend/capabilities.hpp>

namespace acoustic {

struct VoiceReference {
    AudioView audio{};
    std::string language{};
};

struct SpeechSynthesisRequest {
    std::string text{};
    std::string language{};
    std::optional<VoiceReference> voice_reference{};
    double speed{1.0};
};

class ISpeechSynthesizer {
public:
    virtual ~ISpeechSynthesizer() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual TtsCapabilities capabilities() const = 0;
    [[nodiscard]] virtual AudioBuffer synthesize(const SpeechSynthesisRequest& request) const = 0;
};

}  // namespace acoustic
