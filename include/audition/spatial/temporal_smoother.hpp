#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>

#include <audition/core/export.hpp>
#include <audition/spatial/types.hpp>

namespace audition {

/**
 * @brief Policy for dependency-free temporal smoothing of acoustic metric tracks.
 *
 * measurement_weight is the weight of the current measurement in an exact
 * two-component moment mixture with the previous track state. It is not a
 * probability or a Kalman gain.
 *
 * Process variances are explicit random-walk variance rates. They default to
 * zero so libaudition does not invent source-motion uncertainty. Applications
 * that expect moving acoustic sources should configure them from domain data.
 */
struct TemporalSpatialSmootherOptions {
    double range_measurement_weight{0.35};
    double position_measurement_weight{0.35};

    double range_process_variance_m2_per_s{0.0};
    double position_process_variance_m2_per_s{0.0};

    Duration max_gap{2'000'000'000LL};
    std::size_t max_tracks{128U};
};

/**
 * @brief Stateful acoustic range/position smoother keyed by SpatialTrackId.
 *
 * The smoother operates only on metric acoustic estimates already present in a
 * SpatialTrack. Direction tracking remains the responsibility of the spatial
 * backend (for example ODAS).
 *
 * For each update, previous and current estimates are combined by exact mixture
 * first/second moments. This deliberately avoids pretending that successive
 * estimates are statistically independent. Disagreement therefore increases
 * output variance instead of producing unjustified over-confidence.
 *
 * Missing range/position fields are left missing and do not advance that
 * quantity's temporal state. A gap greater than max_gap reinitializes the
 * corresponding state from the new measurement.
 */
class AUDITION_API TemporalSpatialTrackSmoother {
public:
    explicit TemporalSpatialTrackSmoother(
        TemporalSpatialSmootherOptions options = {});

    /**
     * @brief Smooth metric fields in-place and update internal track state.
     */
    void update(SpatialTrack& track);

    /**
     * @brief Forget one tracker identity.
     *
     * Call this when the upstream tracker ends a SpatialTrackId so a future
     * numeric-ID reuse cannot inherit stale smoothing state.
     */
    void endTrack(SpatialTrackId track_id) noexcept;

    /**
     * @brief Clear all transient smoothing state.
     */
    void reset() noexcept;

    [[nodiscard]] std::size_t trackedCount() const noexcept;

    [[nodiscard]] const TemporalSpatialSmootherOptions&
    options() const noexcept;

private:
    struct RangeState {
        Gaussian1D estimate{};
        Timestamp timestamp{};
    };

    struct PositionState {
        Vec3 mean{};
        Covariance3 covariance{};
        Timestamp timestamp{};
    };

    struct TrackState {
        std::optional<RangeState> range{};
        std::optional<PositionState> position{};
    };

    TemporalSpatialSmootherOptions options_{};
    std::unordered_map<
        SpatialTrackId,
        TrackState,
        StrongIdHash<SpatialTrackId>> states_{};
};

}  // namespace audition
