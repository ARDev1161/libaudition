#pragma once

#include <optional>
#include <string>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/audio/audio_quality.hpp>
#include <audition/core/id.hpp>
#include <audition/core/probability.hpp>

namespace audition {

struct SpeechSegment {
    SpeechSegmentId segment_id{};
    std::optional<SpatialTrackId> track_id{};
    std::optional<AcousticSourceId> source_id{};
    AudioBuffer audio{};
    AudioQuality quality{};
};

struct WordTimestamp {
    std::string text{};
    Duration start_offset{};
    Duration end_offset{};
    std::optional<Probability> confidence{};
};

struct Transcript {
    SpeechSegmentId segment_id{};
    std::string text{};
    std::string language{};
    std::optional<Probability> language_confidence{};
    std::optional<Probability> confidence{};
    std::vector<WordTimestamp> words{};
};

struct VadResult {
    Probability speech_probability{Probability::zero()};
    bool speech_active{false};
};

struct KeywordHit {
    std::string keyword{};
    Probability probability{Probability::zero()};
    Duration offset{};
};

}  // namespace audition
