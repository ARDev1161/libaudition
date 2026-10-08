#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>

#include <audition/core/export.hpp>
#include <audition/event/acoustic_event.hpp>
#include <audition/interfaces/source_identity.hpp>
#include <audition/interfaces/spatial.hpp>
#include <audition/spatial/types.hpp>

namespace audition {

/**
 * @brief Backend-independent integration of spatial fusion and persistent source identity.
 *
 * The coordinator owns no fusion or identity implementation. It composes an
 * ISpatialFusion with an ISourceIdentityResolver and writes the resulting
 * persistent source identity back into backend-neutral spatial domain objects.
 *
 * SpatialTrack::source_id is treated as coordinator output. observe() replaces
 * any pre-existing value with the resolver decision. propagate() is stricter:
 * already-populated conflicting IDs are rejected instead of silently overwritten.
 */
class AUDITION_API SpatialIdentityCoordinator {
public:
    SpatialIdentityCoordinator(
        ISpatialFusion& fusion,
        ISourceIdentityResolver& resolver) noexcept;

    /**
     * @brief Fuse optional spatial observations and resolve persistent identity.
     *
     * fusion_input belongs to the same source hypothesis as track. Every fusion
     * observation must use the same clock identity as track.last_seen and must
     * not be timestamped later than track.last_seen.
     *
     * When fusion produces a position, it replaces track.position before the
     * identity observation is submitted. If fusion is empty or underconstrained,
     * the track's existing position is preserved.
     */
    [[nodiscard]] SourceIdentityDecision observe(
        SpatialTrack& track,
        const SpatialFusionInput& fusion_input = {});

    /**
     * @brief End one active tracker binding in the resolver and coordinator.
     */
    void endTrack(SpatialTrackId track_id, Timestamp timestamp);

    /**
     * @brief Clear transient integration state and reset the resolver.
     */
    void reset();

    /**
     * @brief Return the persistent source currently bound to an active track.
     */
    [[nodiscard]] std::optional<AcousticSourceId> activeSource(
        SpatialTrackId track_id) const noexcept;

    /**
     * @brief Propagate known active source IDs to tracks and separated frames.
     *
     * Returns the number of objects that were newly annotated.
     */
    [[nodiscard]] std::size_t propagate(
        SpatialProcessingResult& result) const;

    /**
     * @brief Propagate an active source ID to one separated frame.
     *
     * Returns false when the track has no active binding.
     */
    [[nodiscard]] bool propagate(
        TrackedAudioFrame& frame) const;

    /**
     * @brief Attach a resolved track/source association to an event.
     *
     * The track must already contain source_id. If the event already names a
     * different track or source, InvalidState is thrown.
     */
    static void attachToEvent(
        AcousticEvent& event,
        const SpatialTrack& track);

private:
    ISpatialFusion* fusion_{nullptr};
    ISourceIdentityResolver* resolver_{nullptr};

    std::unordered_map<
        SpatialTrackId,
        AcousticSourceId,
        StrongIdHash<SpatialTrackId>> active_sources_{};
};

}  // namespace audition
