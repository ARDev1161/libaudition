#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/backend/capabilities.hpp>
#include <audition/speaker/types.hpp>

namespace audition {

class ISpeakerEmbedder {
public:
    virtual ~ISpeakerEmbedder() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AudioRequirements audioRequirements() const = 0;
    [[nodiscard]] virtual std::size_t embeddingDimension() const = 0;
    [[nodiscard]] virtual SpeakerEmbedding embed(AudioView speech) const = 0;
};

class ISpeakerVerifier {
public:
    virtual ~ISpeakerVerifier() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual SpeakerVerificationResult compare(
        const SpeakerEmbedding& reference,
        const SpeakerEmbedding& candidate,
        Score threshold) const = 0;
};

/**
 * In-memory computational identification index.
 *
 * This interface is deliberately not persistent storage. Applications can
 * repopulate it from ISpeakerRegistry or another repository.
 */
class ISpeakerIdentifier {
public:
    virtual ~ISpeakerIdentifier() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    virtual void clear() = 0;
    virtual void enroll(const SpeakerEnrollment& enrollment) = 0;
    [[nodiscard]] virtual bool remove(SpeakerId speaker_id) = 0;
    [[nodiscard]] virtual std::optional<SpeakerIdentity> identify(
        const SpeakerEmbedding& candidate,
        Score threshold) const = 0;
    [[nodiscard]] virtual std::vector<SpeakerIdentity> identifyTopK(
        const SpeakerEmbedding& candidate,
        Score threshold,
        std::size_t max_results) const = 0;
};

class ISpeakerDiarizer {
public:
    virtual ~ISpeakerDiarizer() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AudioRequirements audioRequirements() const = 0;
    [[nodiscard]] virtual SpeakerDiarizationResult diarize(AudioView audio) const = 0;
};

}  // namespace audition
