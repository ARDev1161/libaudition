#pragma once

#include <optional>

#include <acoustic/core/probability.hpp>

namespace acoustic {

struct AuthenticityResult {
    Probability bona_fide_probability{Probability::zero()};
    std::optional<Probability> replay_probability{};
    std::optional<Probability> synthetic_probability{};
};

}  // namespace acoustic
