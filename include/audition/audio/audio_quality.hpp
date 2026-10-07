#pragma once

#include <optional>

#include <audition/core/probability.hpp>

namespace audition {

struct AudioQuality {
    std::optional<double> snr_db{};
    std::optional<double> clipping_ratio{};
    std::optional<Probability> speech_probability{};
    std::optional<Probability> separation_quality{};
    std::optional<double> reverberation_seconds{};
};

}  // namespace audition
