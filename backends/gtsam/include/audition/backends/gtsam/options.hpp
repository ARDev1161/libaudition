#pragma once

#include <audition/core/export.hpp>

namespace audition {

struct GtsamSpatialFusionOptions {
    double default_bearing_sigma_rad{0.08726646259971647};
    double minimum_bearing_sigma_rad{1e-4};
    double minimum_range_sigma_m{1e-3};
    double minimum_position_sigma_m{1e-3};
    double initialization_depth_m{2.0};
};

AUDITION_API void validateGtsamSpatialFusionOptions(
    const GtsamSpatialFusionOptions& options);

}  // namespace audition
