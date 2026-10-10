#pragma once

#include <optional>
#include <string>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/audio/audio_quality.hpp>
#include <audition/core/geometry.hpp>
#include <audition/core/id.hpp>

namespace audition {

struct SourceFingerprint {
    std::string model_id{};
    std::vector<float> embedding{};
    Probability quality{Probability::zero()};
};

struct SpatialTrack {
    SpatialTrackId track_id{};
    std::optional<AcousticSourceId> source_id{};
    DirectionEstimate direction{};
    std::optional<RangeEstimate> range{};
    std::optional<PositionEstimate> position{};
    Probability activity{Probability::zero()};
    AudioQuality quality{};
    Timestamp first_seen{};
    Timestamp last_seen{};
    std::optional<SourceFingerprint> fingerprint{};
};

struct TrackedAudioFrame {
    SpatialTrackId track_id{};
    std::optional<AcousticSourceId> source_id{};
    DirectionEstimate direction{};
    Probability activity{Probability::zero()};
    AudioQuality quality{};
    AudioBuffer audio{};
};

// Short-lived SSL direction proposal, not a tracked/acoustically identified
// source. 'score' is a raw backend detection statistic, not a probability.
struct SpatialPotentialSource {
    Direction3D direction{Direction3D::fromVector({1.0, 0.0, 0.0})};
    double score{0.0};
};

struct SpatialProcessingResult {
    std::vector<SpatialTrack> tracks{};
    std::vector<TrackedAudioFrame> separated_frames{};
    std::vector<SpatialPotentialSource> potential_sources{};
};

}  // namespace audition
