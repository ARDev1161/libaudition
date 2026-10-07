#pragma once

#include <optional>

#include <audition/core/probability.hpp>

namespace audition {

struct AuthenticityResult {
    Probability bona_fide_probability{Probability::zero()};
    std::optional<Probability> replay_probability{};
    std::optional<Probability> synthetic_probability{};
};

}  // namespace audition
