#pragma once

#include <cstddef>

#include <audition/core/export.hpp>
#include <audition/interfaces/spatial.hpp>

namespace audition {

struct GtsamSpatialFusionOptions {
    double default_bearing_sigma_rad{0.15};
    double default_range_sigma_m{0.50};
    double sensor_position_sigma_m{1e-6};
    double minimum_sigma{1e-6};
    double fallback_initial_range_m{2.0};
    std::size_t max_iterations{50};
};

/**
 * @brief Batch bearing/range/position fusion backed by GTSAM.
 *
 * Each SpatialFusionInput represents observations of one source hypothesis.
 * Bearing observations are expressed in the local sensor frame and transformed
 * through BearingObservation::sensor_pose. Range observations are anchored by
 * ScalarRangeObservation::sensor_pose. Position observations are already in the
 * canonical world frame.
 *
 * The backend does not manufacture a calibrated confidence probability. The
 * returned covariance is the authoritative uncertainty result and confidence is
 * left at Probability::zero().
 */
class AUDITION_API GtsamSpatialFusion final : public ISpatialFusion {
public:
    explicit GtsamSpatialFusion(GtsamSpatialFusionOptions options = {});

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] std::optional<PositionEstimate> fuse(
        const SpatialFusionInput& input) const override;

private:
    GtsamSpatialFusionOptions options_{};
};

}  // namespace audition
