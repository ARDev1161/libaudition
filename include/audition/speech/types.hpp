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

struct TimedToken {
    std::string text{};
    Duration start_offset{};
    std::optional<Duration> end_offset{};
    std::optional<Probability> confidence{};
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
    std::vector<TimedToken> tokens{};
    std::vector<WordTimestamp> words{};
};

struct VadResult {
    std::optional<Probability> speech_probability{};
    bool speech_active{false};
};

struct KeywordHit {
    std::string keyword{};
    std::optional<Probability> probability{};
    Duration offset{};
};

}  // namespace audition
