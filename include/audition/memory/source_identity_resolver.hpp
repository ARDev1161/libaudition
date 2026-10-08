#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include <audition/interfaces/memory.hpp>
#include <audition/interfaces/source_identity.hpp>

namespace audition {

/**
 * @brief Deterministic backend-independent source association policy.
 *
 * Active SpatialTrackId continuity is authoritative until endTrack(). After a
 * track ends, the resolver can reacquire the persistent AcousticSourceId using
 * compatible acoustic fingerprints and/or recent geometry.
 *
 * The registry owns persistent fingerprints and IDs. reset() clears only
 * transient track/geometry state; registry contents are intentionally retained.
 */
struct HeuristicSourceIdentityResolverOptions {
    Duration max_reacquisition_age{5'000'000'000LL};

    double max_position_distance_m{1.5};
    double max_range_delta_m{1.0};
    double max_direction_angle_rad{0.70};

    double min_fingerprint_cosine_similarity{0.80};
    double min_fingerprint_quality{0.25};
    double association_threshold{0.75};

    double fingerprint_weight{0.65};
    double position_weight{0.25};
    double range_weight{0.05};
    double direction_weight{0.05};

    std::size_t max_active_tracks{128U};
    std::size_t max_recent_sources{512U};
};

class HeuristicSourceIdentityResolver final : public ISourceIdentityResolver {
public:
    explicit HeuristicSourceIdentityResolver(
        IAcousticSourceRegistry& registry,
        HeuristicSourceIdentityResolverOptions options = {});

    void reset() override;

    [[nodiscard]] SourceIdentityDecision observe(
        const SourceIdentityObservation& observation) override;

    void endTrack(SpatialTrackId track_id, Timestamp timestamp) override;

private:
    struct TrackBinding {
        AcousticSourceId source_id{};
        Timestamp last_seen{};
    };

    struct SourceState {
        Timestamp last_seen{};
        std::optional<DirectionEstimate> direction{};
        std::optional<RangeEstimate> range{};
        std::optional<PositionEstimate> position{};
    };

    IAcousticSourceRegistry* registry_{nullptr};
    HeuristicSourceIdentityResolverOptions options_{};

    std::optional<Timestamp> last_timestamp_{};
    std::unordered_map<
        SpatialTrackId,
        TrackBinding,
        StrongIdHash<SpatialTrackId>> active_tracks_{};
    std::unordered_map<
        AcousticSourceId,
        SourceState,
        StrongIdHash<AcousticSourceId>> recent_sources_{};

    void validateAndAdvanceTimestamp(Timestamp timestamp);
    void pruneRecentSources(Timestamp timestamp);
    void updateSourceState(
        AcousticSourceId source_id,
        const SourceIdentityObservation& observation);
    void maybeStoreFingerprint(
        AcousticSourceId source_id,
        const SourceIdentityObservation& observation);
};

}  // namespace audition
