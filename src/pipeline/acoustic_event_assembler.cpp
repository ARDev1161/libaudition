#include <audition/pipeline/acoustic_event_assembler.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <audition/core/error.hpp>

namespace audition {
namespace {

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

void validateRange(const RangeEstimate& range) {
    if (!range.distance_m.valid() ||
        range.distance_m.mean < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent range must have finite non-negative mean and variance"};
    }
}

void validatePosition(const PositionEstimate& position) {
    if (!finite(position.mean_m.x) ||
        !finite(position.mean_m.y) ||
        !finite(position.mean_m.z)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent position mean must be finite"};
    }

    for (const double value : position.covariance_m2) {
        if (!finite(value)) {
            throw Error{
                ErrorCode::InvalidArgument,
                "AcousticEvent position covariance must be finite"};
        }
    }
}

void validateDirection(const DirectionEstimate& direction) {
    if (direction.angular_variance_rad2.has_value() &&
        (!finite(*direction.angular_variance_rad2) ||
         *direction.angular_variance_rad2 < 0.0)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent direction variance must be finite and non-negative"};
    }
}

void validatePatchFields(const AcousticEventPatch& patch) {
    if (patch.track_id.has_value() &&
        !patch.track_id->valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent patch contains an invalid SpatialTrackId"};
    }

    if (patch.source_id.has_value() &&
        !patch.source_id->valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent patch contains an invalid AcousticSourceId"};
    }

    if (patch.direction.has_value()) {
        validateDirection(*patch.direction);
    }
    if (patch.range.has_value()) {
        validateRange(*patch.range);
    }
    if (patch.position.has_value()) {
        validatePosition(*patch.position);
    }

    if (patch.transcript.has_value() &&
        !patch.transcript->segment_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent transcript requires a valid SpeechSegmentId"};
    }

    if (patch.speaker.has_value() &&
        !patch.speaker->speaker_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent speaker annotation requires a valid SpeakerId"};
    }
}

void validatePatchTime(
    const AcousticEvent& event,
    Timestamp timestamp) {
    if (!timestamp.comparableWith(event.start_time)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "AcousticEvent updates must use the event ClockIdentity"};
    }

    if (timestamp.nanoseconds() <
        event.end_time.nanoseconds()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent updates must be non-decreasing in time"};
    }
}

void applyIdentity(
    AcousticEvent& event,
    const AcousticEventPatch& patch) {
    if (patch.source_id.has_value()) {
        if (event.source_id.has_value() &&
            *event.source_id != *patch.source_id) {
            throw Error{
                ErrorCode::InvalidState,
                "AcousticEvent cannot change persistent AcousticSourceId"};
        }
        event.source_id = *patch.source_id;
    }

    if (!patch.track_id.has_value()) {
        return;
    }

    if (!event.track_id.has_value() ||
        *event.track_id == *patch.track_id) {
        event.track_id = *patch.track_id;
        return;
    }

    // A transient tracker handoff is safe only when this update explicitly
    // carries the same persistent source already attached to the event.
    if (event.source_id.has_value() &&
        patch.source_id.has_value() &&
        *event.source_id == *patch.source_id) {
        event.track_id = *patch.track_id;
        return;
    }

    throw Error{
        ErrorCode::InvalidState,
        "AcousticEvent track change requires matching persistent source identity"};
}

void applyPatch(
    AcousticEvent& event,
    const AcousticEventPatch& patch) {
    validatePatchFields(patch);
    validatePatchTime(event, patch.timestamp);
    applyIdentity(event, patch);

    if (patch.direction.has_value()) {
        event.direction = patch.direction;
    }
    if (patch.range.has_value()) {
        event.range = patch.range;
    }
    if (patch.position.has_value()) {
        event.position = patch.position;
    }
    if (patch.classification.has_value()) {
        event.classification = patch.classification;
    }
    if (patch.transcript.has_value()) {
        event.transcript = patch.transcript;
    }
    if (patch.speaker.has_value()) {
        event.speaker = patch.speaker;
    }
    if (patch.voice_traits.has_value()) {
        event.voice_traits = patch.voice_traits;
    }
    if (patch.voice_state.has_value()) {
        event.voice_state = patch.voice_state;
    }
    if (patch.authenticity.has_value()) {
        event.authenticity = patch.authenticity;
    }

    event.end_time = patch.timestamp;
}

void validateTrack(const SpatialTrack& track) {
    if (!track.track_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent track update requires a valid SpatialTrackId"};
    }
    if (!track.first_seen.comparableWith(track.last_seen)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "SpatialTrack first_seen and last_seen must use the same clock identity"};
    }
    if (track.last_seen.nanoseconds() <
        track.first_seen.nanoseconds()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SpatialTrack last_seen must not precede first_seen"};
    }
    if (track.source_id.has_value() &&
        !track.source_id->valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SpatialTrack contains an invalid AcousticSourceId"};
    }
}

}  // namespace

AcousticEventAssembler::AcousticEventAssembler(
    AcousticEventAssemblerOptions options)
    : options_(options) {
    if (options_.max_active_events == 0U) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent max_active_events must be non-zero"};
    }
}

AcousticEventId AcousticEventAssembler::begin(
    Timestamp start_time) {
    if (active_.size() >= options_.max_active_events) {
        throw Error{
            ErrorCode::InvalidState,
            "AcousticEvent active-event capacity exceeded"};
    }
    if (next_event_id_ == 0U) {
        throw Error{
            ErrorCode::InvalidState,
            "AcousticEvent ID space exhausted"};
    }

    const AcousticEventId event_id{next_event_id_};
    if (next_event_id_ ==
        std::numeric_limits<AcousticEventId::value_type>::max()) {
        next_event_id_ = 0U;
    } else {
        ++next_event_id_;
    }

    AcousticEvent event{};
    event.event_id = event_id;
    event.start_time = start_time;
    event.end_time = start_time;

    active_.emplace(event_id, std::move(event));
    return event_id;
}

void AcousticEventAssembler::update(
    AcousticEventId event_id,
    const AcousticEventPatch& patch) {
    if (!event_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent update requires a valid event ID"};
    }

    const auto it = active_.find(event_id);
    if (it == active_.end()) {
        throw Error{
            ErrorCode::InvalidState,
            "AcousticEvent update references an inactive event"};
    }

    AcousticEvent candidate = it->second;
    applyPatch(candidate, patch);
    it->second = std::move(candidate);
}

void AcousticEventAssembler::updateFromTrack(
    AcousticEventId event_id,
    const SpatialTrack& track) {
    update(event_id, patchFromTrack(track));
}

AcousticEvent AcousticEventAssembler::finish(
    AcousticEventId event_id,
    Timestamp end_time) {
    if (!event_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent finish requires a valid event ID"};
    }

    const auto it = active_.find(event_id);
    if (it == active_.end()) {
        throw Error{
            ErrorCode::InvalidState,
            "AcousticEvent finish references an inactive event"};
    }

    if (!end_time.comparableWith(it->second.start_time)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "AcousticEvent finish must use the event ClockIdentity"};
    }
    if (end_time.nanoseconds() <
        it->second.end_time.nanoseconds()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AcousticEvent end_time must not precede the latest update"};
    }

    AcousticEvent result = it->second;
    result.end_time = end_time;
    active_.erase(it);
    return result;
}

void AcousticEventAssembler::cancel(
    AcousticEventId event_id) noexcept {
    active_.erase(event_id);
}

void AcousticEventAssembler::reset() noexcept {
    active_.clear();
}

std::optional<AcousticEvent>
AcousticEventAssembler::find(
    AcousticEventId event_id) const {
    const auto it = active_.find(event_id);
    if (it == active_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<AcousticEvent>
AcousticEventAssembler::activeEvents() const {
    std::vector<AcousticEvent> result;
    result.reserve(active_.size());

    for (const auto& item : active_) {
        result.push_back(item.second);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const AcousticEvent& lhs,
           const AcousticEvent& rhs) {
            return lhs.event_id.value() <
                   rhs.event_id.value();
        });

    return result;
}

std::size_t
AcousticEventAssembler::activeCount() const noexcept {
    return active_.size();
}

const AcousticEventAssemblerOptions&
AcousticEventAssembler::options() const noexcept {
    return options_;
}

AcousticEventPatch
AcousticEventAssembler::patchFromTrack(
    const SpatialTrack& track) {
    validateTrack(track);

    AcousticEventPatch patch{};
    patch.timestamp = track.last_seen;
    patch.track_id = track.track_id;
    patch.source_id = track.source_id;
    patch.direction = track.direction;
    patch.range = track.range;
    patch.position = track.position;
    return patch;
}

}  // namespace audition
