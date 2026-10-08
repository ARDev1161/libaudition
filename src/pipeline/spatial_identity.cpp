#include <audition/pipeline/spatial_identity.hpp>

#include <cstddef>
#include <optional>
#include <string>

#include <audition/core/error.hpp>

namespace audition {
namespace {

[[nodiscard]] bool hasFusionObservations(
    const SpatialFusionInput& input) noexcept {
    return !input.bearings.empty() ||
           !input.ranges.empty() ||
           !input.positions.empty();
}

void validateTrack(const SpatialTrack& track) {
    if (!track.track_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Spatial identity integration requires a valid track ID"};
    }

    if (!track.first_seen.comparableWith(track.last_seen)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "Spatial track first_seen and last_seen must use the same clock identity"};
    }

    if (track.last_seen.nanoseconds() < track.first_seen.nanoseconds()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Spatial track last_seen must not precede first_seen"};
    }
}

void validateFusionTimestamp(
    const Timestamp& timestamp,
    const Timestamp& track_timestamp) {
    if (!timestamp.comparableWith(track_timestamp)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "Fusion observations and spatial track must use the same clock identity"};
    }

    if (timestamp.nanoseconds() > track_timestamp.nanoseconds()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Fusion observation timestamp must not be later than track.last_seen"};
    }
}

void validateFusionTimes(
    const SpatialFusionInput& input,
    const Timestamp& track_timestamp) {
    for (const auto& observation : input.bearings) {
        validateFusionTimestamp(observation.timestamp, track_timestamp);
    }
    for (const auto& observation : input.ranges) {
        validateFusionTimestamp(observation.timestamp, track_timestamp);
    }
    for (const auto& observation : input.positions) {
        validateFusionTimestamp(observation.timestamp, track_timestamp);
    }
}

[[nodiscard]] bool assignOrVerify(
    std::optional<AcousticSourceId>& destination,
    AcousticSourceId source_id,
    const char* object_name) {
    if (!source_id.valid()) {
        throw Error{
            ErrorCode::InvalidState,
            "Identity resolver returned an invalid source ID"};
    }

    if (!destination.has_value()) {
        destination = source_id;
        return true;
    }

    if (*destination != source_id) {
        throw Error{
            ErrorCode::InvalidState,
            std::string{object_name} +
                " already carries a conflicting acoustic source ID"};
    }

    return false;
}

}  // namespace

SpatialIdentityCoordinator::SpatialIdentityCoordinator(
    ISpatialFusion& fusion,
    ISourceIdentityResolver& resolver) noexcept
    : fusion_(&fusion),
      resolver_(&resolver) {}

SourceIdentityDecision SpatialIdentityCoordinator::observe(
    SpatialTrack& track,
    const SpatialFusionInput& fusion_input) {
    validateTrack(track);
    validateFusionTimes(fusion_input, track.last_seen);

    std::optional<PositionEstimate> resolved_position =
        track.position;
    if (hasFusionObservations(fusion_input)) {
        const auto fused_position = fusion_->fuse(fusion_input);
        if (fused_position.has_value()) {
            resolved_position = *fused_position;
        }
    }

    SourceIdentityObservation identity_observation{};
    identity_observation.track_id = track.track_id;
    identity_observation.timestamp = track.last_seen;
    identity_observation.direction = track.direction;
    identity_observation.range = track.range;
    identity_observation.position = resolved_position;
    identity_observation.fingerprint = track.fingerprint;

    const SourceIdentityDecision decision =
        resolver_->observe(identity_observation);
    if (!decision.source_id.valid()) {
        throw Error{
            ErrorCode::InvalidState,
            "Identity resolver returned an invalid source ID"};
    }

    track.position = resolved_position;
    track.source_id = decision.source_id;
    active_sources_[track.track_id] = decision.source_id;
    return decision;
}

void SpatialIdentityCoordinator::endTrack(
    SpatialTrackId track_id,
    Timestamp timestamp) {
    if (!track_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Spatial identity endTrack requires a valid track ID"};
    }

    resolver_->endTrack(track_id, timestamp);
    active_sources_.erase(track_id);
}

void SpatialIdentityCoordinator::reset() {
    resolver_->reset();
    active_sources_.clear();
}

std::optional<AcousticSourceId>
SpatialIdentityCoordinator::activeSource(
    SpatialTrackId track_id) const noexcept {
    const auto it = active_sources_.find(track_id);
    if (it == active_sources_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t SpatialIdentityCoordinator::propagate(
    SpatialProcessingResult& result) const {
    std::size_t annotated = 0U;

    for (auto& track : result.tracks) {
        const auto source_id = activeSource(track.track_id);
        if (!source_id.has_value()) {
            continue;
        }
        if (assignOrVerify(track.source_id, *source_id, "SpatialTrack")) {
            ++annotated;
        }
    }

    for (auto& frame : result.separated_frames) {
        const auto source_id = activeSource(frame.track_id);
        if (!source_id.has_value()) {
            continue;
        }
        if (assignOrVerify(
                frame.source_id,
                *source_id,
                "TrackedAudioFrame")) {
            ++annotated;
        }
    }

    return annotated;
}

bool SpatialIdentityCoordinator::propagate(
    TrackedAudioFrame& frame) const {
    const auto source_id = activeSource(frame.track_id);
    if (!source_id.has_value()) {
        return false;
    }

    static_cast<void>(
        assignOrVerify(
            frame.source_id,
            *source_id,
            "TrackedAudioFrame"));
    return true;
}

void SpatialIdentityCoordinator::attachToEvent(
    AcousticEvent& event,
    const SpatialTrack& track) {
    if (!track.track_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Cannot attach event identity from an invalid track ID"};
    }
    if (!track.source_id.has_value() ||
        !track.source_id->valid()) {
        throw Error{
            ErrorCode::InvalidState,
            "Cannot attach event identity from an unresolved spatial track"};
    }

    if (event.track_id.has_value() &&
        *event.track_id != track.track_id) {
        throw Error{
            ErrorCode::InvalidState,
            "AcousticEvent already carries a conflicting spatial track ID"};
    }

    event.track_id = track.track_id;
    static_cast<void>(
        assignOrVerify(
            event.source_id,
            *track.source_id,
            "AcousticEvent"));
}

}  // namespace audition
