#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <acoustic/core/id.hpp>
#include <acoustic/interfaces/memory.hpp>

namespace acoustic {

class InMemoryAcousticSourceRegistry final : public IAcousticSourceRegistry {
public:
    AcousticSourceId create() override;
    void addFingerprint(AcousticSourceId id, SourceFingerprint fingerprint) override;
    void associateSpeaker(AcousticSourceId id, SpeakerId speaker_id) override;
    [[nodiscard]] std::optional<AcousticSourceProfile> find(AcousticSourceId id) const override;
    [[nodiscard]] std::vector<AcousticSourceProfile> list() const override;

private:
    mutable std::mutex mutex_{};
    std::uint64_t next_id_{1};
    std::unordered_map<AcousticSourceId, AcousticSourceProfile, StrongIdHash<AcousticSourceId>> data_{};
};

class InMemorySpeakerRegistry final : public ISpeakerRegistry {
public:
    SpeakerId create(std::string display_name = {}) override;
    void addEmbedding(SpeakerId id, SpeakerEmbedding embedding) override;
    void rename(SpeakerId id, std::string display_name) override;
    [[nodiscard]] std::optional<SpeakerProfile> find(SpeakerId id) const override;
    [[nodiscard]] std::vector<SpeakerProfile> list() const override;

private:
    mutable std::mutex mutex_{};
    std::uint64_t next_id_{1};
    std::unordered_map<SpeakerId, SpeakerProfile, StrongIdHash<SpeakerId>> data_{};
};

class InMemorySoundPrototypeRegistry final : public ISoundPrototypeRegistry {
public:
    SoundPrototypeId create(std::string label) override;
    void addExample(SoundPrototypeId id, AudioEmbedding embedding) override;
    [[nodiscard]] std::optional<SoundPrototype> find(SoundPrototypeId id) const override;
    [[nodiscard]] std::vector<SoundPrototype> list() const override;

private:
    mutable std::mutex mutex_{};
    std::uint64_t next_id_{1};
    std::unordered_map<SoundPrototypeId, SoundPrototype, StrongIdHash<SoundPrototypeId>> data_{};
};

}  // namespace acoustic
