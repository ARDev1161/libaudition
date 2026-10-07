#pragma once

#include <optional>

#include <acoustic/spatial/types.hpp>

namespace acoustic {

struct SourceIdentityObservation {
    SpatialTrackId track_id{};
    Timestamp timestamp{};
    DirectionEstimate direction{};
    std::optional<RangeEstimate> range{};
    std::optional<PositionEstimate> position{};
    std::optional<SourceFingerprint> fingerprint{};
};

struct SourceIdentityDecision {
    AcousticSourceId source_id{};
    Probability confidence{Probability::zero()};
    bool newly_created{false};
};

/**
 * @brief Resolves temporary spatial tracks into persistent acoustic source IDs.
 *
 * Implementations may combine track continuity, geometry, time, and acoustic
 * fingerprints. A source is intentionally generic and is not assumed to be a person.
 */
class ISourceIdentityResolver {
public:
    virtual ~ISourceIdentityResolver() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual SourceIdentityDecision observe(
        const SourceIdentityObservation& observation) = 0;
    virtual void endTrack(SpatialTrackId track_id, Timestamp timestamp) = 0;
};

}  // namespace acoustic
