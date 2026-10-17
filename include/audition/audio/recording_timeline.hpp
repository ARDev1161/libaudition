#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace audition {

// All offsets are relative to the FIRST sample of the referenced recording.
// RecordingId is a session-local generation-qualified identity, not ODAS ID.
using RecordingId = std::uint64_t;
struct TimelineSpeechEvent {
    RecordingId recording_id{0};
    std::uint64_t start_sample{0};
    std::uint64_t end_sample{0};
    std::string text;
    std::string language;
};
struct TimelineSoundEvent {
    RecordingId recording_id{0};
    std::uint64_t start_sample{0};
    std::uint64_t end_sample{0};
    std::string label;
    std::string model;
    double confidence{0.0};
    std::string stage;
};

struct TimelineGapEvent {
    RecordingId recording_id{0};
    std::uint64_t start_sample{0};
    std::uint64_t end_sample{0};
    std::string reason{"recorder_backpressure"};
};

// Library-owned annotation model; no GUI, codec, or ML-backend dependency.
class RecordingTimeline {
public:
    explicit RecordingTimeline(RecordingId id, std::uint32_t sample_rate_hz)
        : id_(id), rate_(sample_rate_hz) {
        if (!rate_) throw std::invalid_argument("Timeline sample rate must be positive");
    }
    RecordingId recordingId() const noexcept { return id_; }
    std::uint32_t sampleRateHz() const noexcept { return rate_; }
    void add(TimelineSpeechEvent event) {
        check(event.recording_id, event.start_sample, event.end_sample);
        speech_.push_back(std::move(event));
    }
    void add(TimelineSoundEvent event) {
        check(event.recording_id, event.start_sample, event.end_sample);
        if (!std::isfinite(event.confidence) || event.confidence < 0 || event.confidence > 1)
            throw std::invalid_argument("Invalid classification confidence");
        sound_.push_back(std::move(event));
    }
    void add(TimelineGapEvent event) {
        check(event.recording_id, event.start_sample, event.end_sample);
        if (event.end_sample == event.start_sample) return;
        if (!gaps_.empty() &&
            gaps_.back().end_sample == event.start_sample &&
            gaps_.back().reason == event.reason) {
            gaps_.back().end_sample = event.end_sample;
        } else gaps_.push_back(std::move(event));
    }
    const std::vector<TimelineGapEvent>& gaps() const noexcept { return gaps_; }
    const std::vector<TimelineSpeechEvent>& speech() const noexcept {return speech_;}
    const std::vector<TimelineSoundEvent>& sounds() const noexcept {return sound_;}
private:
    void check(RecordingId id, std::uint64_t start, std::uint64_t end) const {
        if (id != id_ || end < start) throw std::invalid_argument("Invalid timeline event");
    }
    RecordingId id_;
    std::uint32_t rate_;
    std::vector<TimelineSpeechEvent> speech_;
    std::vector<TimelineSoundEvent> sound_;
    std::vector<TimelineGapEvent> gaps_;
};
} // namespace audition
