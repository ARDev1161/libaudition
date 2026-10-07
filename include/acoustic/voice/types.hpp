#pragma once

#include <optional>
#include <string>
#include <vector>

#include <acoustic/core/probability.hpp>

namespace acoustic {

struct VoiceTraitScore {
    std::string trait{};
    Probability probability{Probability::zero()};
};

struct VoiceTraits {
    std::optional<Gaussian1D> estimated_age_years{};
    std::vector<VoiceTraitScore> categorical_traits{};
    std::optional<double> pitch_mean_hz{};
    std::optional<double> pitch_stddev_hz{};
    std::optional<double> speaking_rate_syllables_per_second{};
};

struct VoiceState {
    std::vector<VoiceTraitScore> emotions{};
    std::optional<Probability> stress_probability{};
    std::optional<double> arousal{};
    std::optional<double> valence{};
};

}  // namespace acoustic
