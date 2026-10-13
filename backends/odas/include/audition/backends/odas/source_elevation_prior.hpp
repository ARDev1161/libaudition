#pragma once

#include <cmath>
#include <stdexcept>

#include <audition/backends/odas/odas_options.hpp>

namespace audition {

// Restrict ODAS' *search domain* to directions near the microphone XY plane.
// Intended ONLY when mounting geometry and expected source locations justify
// a near-horizontal (azimuth-centric) assumption, e.g. a robot listening to
// people at approximately the height of its horizontally mounted mic array.
//
// Replaces options.spatial_filters with two opposing +Z/-Z angular masks,
// rather than projecting measured/estimated 3D directions after tracking.
// The ODAS angular mask allows directions within the configured half-band,
// with a soft transition at the boundary. Unconstrained 3D localization
// must NOT use this preset: it can suppress genuinely elevated sources.
// Apply after any array-specific profile if both are desired.
inline void applyOdasNearPlaneSourcePrior(
    OdasOptions& options, double max_elevation_deg = 25.0,
    double transition_deg = 5.0) {
    if (!std::isfinite(max_elevation_deg) || !std::isfinite(transition_deg) ||
        max_elevation_deg <= 0.0 || max_elevation_deg >= 90.0 ||
        transition_deg <= 0.0 || transition_deg >= max_elevation_deg) {
        throw std::invalid_argument{
            "ODAS near-plane source prior requires 0 < transition < max elevation < 90 degrees"};
    }
    const double all_pass = 90.0 + max_elevation_deg - transition_deg;
    const double no_pass = 90.0 + max_elevation_deg;
    options.spatial_filters = {
        OdasSpatialFilter{Direction3D::fromVector({0.0, 0.0, 1.0}),
                          all_pass, no_pass},
        OdasSpatialFilter{Direction3D::fromVector({0.0, 0.0, -1.0}),
                          all_pass, no_pass}
    };
}

}  // namespace audition
