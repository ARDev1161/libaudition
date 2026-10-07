#pragma once

#include <optional>

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/backend/capabilities.hpp>
#include <acoustic/speaker/types.hpp>

namespace acoustic {

class ISpeakerEmbedder {
public:
    virtual ~ISpeakerEmbedder() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual SpeakerEmbedding embed(AudioView speech) const = 0;
};

class ISpeakerVerifier {
public:
    virtual ~ISpeakerVerifier() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual SpeakerMatch compare(
        const SpeakerEmbedding& reference, const SpeakerEmbedding& candidate) const = 0;
};

class ISpeakerIdentifier {
public:
    virtual ~ISpeakerIdentifier() = default;
    [[nodiscard]] virtual std::optional<SpeakerIdentity> identify(
        const SpeakerEmbedding& candidate) const = 0;
};

}  // namespace acoustic
