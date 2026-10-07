#pragma once

#include <optional>
#include <string>
#include <vector>

#include <audition/memory/types.hpp>

namespace audition {

class IAcousticSourceRegistry {
public:
    virtual ~IAcousticSourceRegistry() = default;
    [[nodiscard]] virtual AcousticSourceId create() = 0;
    virtual void addFingerprint(AcousticSourceId id, SourceFingerprint fingerprint) = 0;
    virtual void associateSpeaker(AcousticSourceId id, SpeakerId speaker_id) = 0;
    [[nodiscard]] virtual std::optional<AcousticSourceProfile> find(AcousticSourceId id) const = 0;
    [[nodiscard]] virtual std::vector<AcousticSourceProfile> list() const = 0;
};

class ISpeakerRegistry {
public:
    virtual ~ISpeakerRegistry() = default;
    [[nodiscard]] virtual SpeakerId create(std::string display_name = {}) = 0;
    virtual void addEmbedding(SpeakerId id, SpeakerEmbedding embedding) = 0;
    virtual void rename(SpeakerId id, std::string display_name) = 0;
    [[nodiscard]] virtual std::optional<SpeakerProfile> find(SpeakerId id) const = 0;
    [[nodiscard]] virtual std::vector<SpeakerProfile> list() const = 0;
};

class ISoundPrototypeRegistry {
public:
    virtual ~ISoundPrototypeRegistry() = default;
    [[nodiscard]] virtual SoundPrototypeId create(std::string label) = 0;
    virtual void addExample(SoundPrototypeId id, AudioEmbedding embedding) = 0;
    [[nodiscard]] virtual std::optional<SoundPrototype> find(SoundPrototypeId id) const = 0;
    [[nodiscard]] virtual std::vector<SoundPrototype> list() const = 0;
};

}  // namespace audition
