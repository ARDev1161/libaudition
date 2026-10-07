#pragma once

#include <optional>
#include <string>
#include <vector>

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/audio/audio_quality.hpp>
#include <acoustic/core/geometry.hpp>
#include <acoustic/core/id.hpp>

namespace acoustic {

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

struct SpatialProcessingResult {
    std::vector<SpatialTrack> tracks{};
    std::vector<TrackedAudioFrame> separated_frames{};
};

}  // namespace acoustic
