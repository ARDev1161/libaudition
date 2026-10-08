#pragma once

#include <optional>

#include <audition/core/probability.hpp>

namespace audition {

struct AuthenticityResult {
    std::optional<Score> bona_fide_score{};
    std::optional<Probability> bona_fide_probability{};
    std::optional<Score> spoof_score{};
    std::optional<Probability> spoof_probability{};
    std::optional<Probability> replay_probability{};
    std::optional<Probability> synthetic_probability{};
};

}  // namespace audition
