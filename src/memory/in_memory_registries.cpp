#include <acoustic/memory/in_memory_registries.hpp>

#include <utility>

#include <acoustic/core/error.hpp>

namespace acoustic {
namespace {

template <typename Map, typename Id>
auto& requireEntry(Map& map, Id id, const char* kind) {
    const auto it = map.find(id);
    if (it == map.end()) {
        throw Error{ErrorCode::InvalidArgument, std::string{kind} + " ID does not exist"};
    }
    return it->second;
}

template <typename Map>
auto values(const Map& map) {
    using Value = typename Map::mapped_type;
    std::vector<Value> result;
    result.reserve(map.size());
    for (const auto& item : map) {
        result.push_back(item.second);
    }
    return result;
}

}  // namespace

AcousticSourceId InMemoryAcousticSourceRegistry::create() {
    std::lock_guard<std::mutex> lock{mutex_};
    const AcousticSourceId id{next_id_++};
    data_.emplace(id, AcousticSourceProfile{id});
    return id;
}

void InMemoryAcousticSourceRegistry::addFingerprint(AcousticSourceId id, SourceFingerprint fingerprint) {
    std::lock_guard<std::mutex> lock{mutex_};
    requireEntry(data_, id, "Acoustic source").fingerprints.push_back(std::move(fingerprint));
}

void InMemoryAcousticSourceRegistry::associateSpeaker(AcousticSourceId id, SpeakerId speaker_id) {
    std::lock_guard<std::mutex> lock{mutex_};
    requireEntry(data_, id, "Acoustic source").associated_speaker = speaker_id;
}

std::optional<AcousticSourceProfile> InMemoryAcousticSourceRegistry::find(AcousticSourceId id) const {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = data_.find(id);
    return it == data_.end() ? std::nullopt : std::optional<AcousticSourceProfile>{it->second};
}

std::vector<AcousticSourceProfile> InMemoryAcousticSourceRegistry::list() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return values(data_);
}

SpeakerId InMemorySpeakerRegistry::create(std::string display_name) {
    std::lock_guard<std::mutex> lock{mutex_};
    const SpeakerId id{next_id_++};
    SpeakerProfile profile;
    profile.speaker_id = id;
    profile.display_name = std::move(display_name);
    data_.emplace(id, std::move(profile));
    return id;
}

void InMemorySpeakerRegistry::addEmbedding(SpeakerId id, SpeakerEmbedding embedding) {
    std::lock_guard<std::mutex> lock{mutex_};
    requireEntry(data_, id, "Speaker").embeddings.push_back(std::move(embedding));
}

void InMemorySpeakerRegistry::rename(SpeakerId id, std::string display_name) {
    std::lock_guard<std::mutex> lock{mutex_};
    requireEntry(data_, id, "Speaker").display_name = std::move(display_name);
}

std::optional<SpeakerProfile> InMemorySpeakerRegistry::find(SpeakerId id) const {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = data_.find(id);
    return it == data_.end() ? std::nullopt : std::optional<SpeakerProfile>{it->second};
}

std::vector<SpeakerProfile> InMemorySpeakerRegistry::list() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return values(data_);
}

SoundPrototypeId InMemorySoundPrototypeRegistry::create(std::string label) {
    if (label.empty()) {
        throw Error{ErrorCode::InvalidArgument, "Sound prototype label must not be empty"};
    }
    std::lock_guard<std::mutex> lock{mutex_};
    const SoundPrototypeId id{next_id_++};
    SoundPrototype prototype;
    prototype.prototype_id = id;
    prototype.label = std::move(label);
    data_.emplace(id, std::move(prototype));
    return id;
}

void InMemorySoundPrototypeRegistry::addExample(SoundPrototypeId id, AudioEmbedding embedding) {
    std::lock_guard<std::mutex> lock{mutex_};
    requireEntry(data_, id, "Sound prototype").examples.push_back(std::move(embedding));
}

std::optional<SoundPrototype> InMemorySoundPrototypeRegistry::find(SoundPrototypeId id) const {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = data_.find(id);
    return it == data_.end() ? std::nullopt : std::optional<SoundPrototype>{it->second};
}

std::vector<SoundPrototype> InMemorySoundPrototypeRegistry::list() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return values(data_);
}

}  // namespace acoustic
