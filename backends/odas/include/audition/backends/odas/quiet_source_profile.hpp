#pragma once

#include <audition/backends/odas/odas_options.hpp>

#include <stdexcept>

namespace audition {

// Optional lower-amplitude source acquisition for a ReSpeaker-style array.
// ODAS's default SST active/inactive likelihood means (0.40 / 0.25) can
// require a louder first sound. The more sensitive values (0.30 / 0.15)
// follow the documented upstream ODAS SST tuning guidance.
// This changes only SST activation and intentionally leaves SSL proposals,
// source-birth confirmation thresholds, inactive timeouts and microphone
// samples untouched. More phantom/false tracks are possible.
inline void applyOdasQuietSourceTrackingProfile(OdasOptions& options) {
    if (options.sst.active_gmm.empty() || options.sst.inactive_gmm.empty())
        throw std::invalid_argument(
            "Quiet-source tracking requires SST active and inactive GMMs");
    options.sst.active_gmm.front().mean = 0.30;
    options.sst.inactive_gmm.front().mean = 0.15;
}

} // namespace audition
