#pragma once

#include <audition/backends/odas/odas_options.hpp>

#include <stdexcept>

namespace audition {

// Optional lower-amplitude source acquisition for a ReSpeaker-style array.
// ODAS's default active GMM mean (0.40) may fail to acquire quiet speech.
// Preserve the default inactive mean (0.25), rather than the more permissive
// upstream 0.15: keeping it low can hold phantom tracks alive during silence.
// The 150-hop inactivity window (1.2 s at 16 kHz / 128 samples) follows
// upstream ReSpeaker's shortest example. No SSL or signal gain is modified.
// This is an experiment; the observed speech/false-track tradeoff needs a
// hardware-verified before/after run.
inline void applyOdasQuietSourceTrackingProfile(OdasOptions& options) {
    if (options.sst.active_gmm.empty() || options.sst.inactive_gmm.empty())
        throw std::invalid_argument(
            "Quiet-source tracking requires SST active and inactive GMMs");
    options.sst.active_gmm.front().mean = 0.30;
    options.sst.inactive_gmm.front().mean = 0.25;
    options.sst.inactive_timeout_hops = 150U;
}

} // namespace audition
