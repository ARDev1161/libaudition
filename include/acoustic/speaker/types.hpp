#pragma once

#include <optional>
#include <string>
#include <vector>

#include <acoustic/core/id.hpp>
#include <acoustic/core/probability.hpp>

namespace acoustic {

struct SpeakerEmbedding {
    std::string model_id{};
    std::vector<float> values{};
    Probability quality{Probability::zero()};
};

struct SpeakerMatch {
    SpeakerId speaker_id{};
    Score similarity{};
    std::optional<Probability> calibrated_probability{};
};

struct SpeakerIdentity {
    SpeakerId speaker_id{};
    std::string display_name{};
    Score similarity{};
    std::optional<Probability> calibrated_probability{};
};

}  // namespace acoustic
