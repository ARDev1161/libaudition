#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <acoustic/classify/types.hpp>
#include <acoustic/core/id.hpp>
#include <acoustic/core/time.hpp>
#include <acoustic/spatial/types.hpp>
#include <acoustic/speaker/types.hpp>

namespace acoustic {

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

}  // namespace acoustic
