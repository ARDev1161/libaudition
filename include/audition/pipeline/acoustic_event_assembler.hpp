#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include <audition/core/export.hpp>
#include <audition/event/acoustic_event.hpp>
#include <audition/spatial/types.hpp>

namespace audition {

/**
 * @brief Atomic annotation update for one active AcousticEvent.
 *
 * timestamp is the observation/annotation time. Fields not present in the patch
 * remain unchanged; there is deliberately no implicit "clear" operation.
 */
struct AcousticEventPatch {
    Timestamp timestamp{};

    std::optional<SpatialTrackId> track_id{};
    std::optional<AcousticSourceId> source_id{};

    std::optional<DirectionEstimate> direction{};
    std::optional<RangeEstimate> range{};
    std::optional<PositionEstimate> position{};

    std::optional<ClassificationResult> classification{};
    std::optional<Transcript> transcript{};
    std::optional<SpeakerIdentity> speaker{};
    std::optional<VoiceTraits> voice_traits{};
    std::optional<VoiceState> voice_state{};
    std::optional<AuthenticityResult> authenticity{};
};

struct AcousticEventAssemblerOptions {
    std::size_t max_active_events{128U};
};

/**
 * @brief Explicit lifecycle assembler for backend-neutral acoustic events.
 *
 * The assembler does not detect event boundaries. The caller explicitly begins,
 * updates, and finishes events. This keeps segmentation/scheduling policy out of
 * libaudition while providing deterministic identity/time consistency.
 *
 * Active events may overlap. Updates are transactional: validation is performed
 * against a candidate copy and the stored event is modified only after the whole
 * patch succeeds.
 */
class AUDITION_API AcousticEventAssembler {
public:
    explicit AcousticEventAssembler(
        AcousticEventAssemblerOptions options = {});

    /**
     * @brief Begin an empty event at start_time and return a generated ID.
     */
    [[nodiscard]] AcousticEventId begin(Timestamp start_time);

    /**
     * @brief Apply one atomic patch to an active event.
     */
    void update(
        AcousticEventId event_id,
        const AcousticEventPatch& patch);

    /**
     * @brief Convenience update from the latest acoustic SpatialTrack snapshot.
     */
    void updateFromTrack(
        AcousticEventId event_id,
        const SpatialTrack& track);

    /**
     * @brief Finish and remove an active event.
     *
     * end_time must use the event clock and must not precede the latest update.
     */
    [[nodiscard]] AcousticEvent finish(
        AcousticEventId event_id,
        Timestamp end_time);

    /**
     * @brief Discard one active event without producing a finalized value.
     */
    void cancel(AcousticEventId event_id) noexcept;

    /**
     * @brief Remove all active events. Generated IDs are not reused.
     */
    void reset() noexcept;

    [[nodiscard]] std::optional<AcousticEvent> find(
        AcousticEventId event_id) const;

    /**
     * @brief Deterministic snapshots sorted by AcousticEventId.
     */
    [[nodiscard]] std::vector<AcousticEvent> activeEvents() const;

    [[nodiscard]] std::size_t activeCount() const noexcept;

    [[nodiscard]] const AcousticEventAssemblerOptions&
    options() const noexcept;

    /**
     * @brief Convert a SpatialTrack snapshot into an event patch.
     *
     * The patch timestamp is track.last_seen. Missing range/position remain
     * absent and therefore do not erase previously known event geometry.
     */
    [[nodiscard]] static AcousticEventPatch patchFromTrack(
        const SpatialTrack& track);

private:
    AcousticEventAssemblerOptions options_{};
    AcousticEventId::value_type next_event_id_{1U};

    std::unordered_map<
        AcousticEventId,
        AcousticEvent,
        StrongIdHash<AcousticEventId>> active_{};
};

}  // namespace audition
