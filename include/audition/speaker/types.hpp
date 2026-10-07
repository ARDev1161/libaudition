#pragma once

#include <optional>
#include <string>
#include <vector>

#include <audition/core/id.hpp>
#include <audition/core/probability.hpp>

namespace audition {

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

}  // namespace audition
