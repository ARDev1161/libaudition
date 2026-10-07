#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <audition/classify/types.hpp>
#include <audition/core/id.hpp>
#include <audition/core/time.hpp>
#include <audition/spatial/types.hpp>
#include <audition/speaker/types.hpp>

namespace audition {

struct AcousticSourceProfile {
    AcousticSourceId source_id{};
    std::vector<SourceFingerprint> fingerprints{};
    std::optional<SpeakerId> associated_speaker{};
    std::optional<Timestamp> first_seen{};
    std::optional<Timestamp> last_seen{};
    std::map<std::string, std::string> metadata{};
};

struct SpeakerProfile {
    SpeakerId speaker_id{};
    std::string display_name{};
    std::vector<SpeakerEmbedding> embeddings{};
    std::optional<Timestamp> first_seen{};
    std::optional<Timestamp> last_seen{};
    std::map<std::string, std::string> metadata{};
};

struct SoundPrototype {
    SoundPrototypeId prototype_id{};
    std::string label{};
    std::vector<AudioEmbedding> examples{};
    std::map<std::string, std::string> metadata{};
};

}  // namespace audition
