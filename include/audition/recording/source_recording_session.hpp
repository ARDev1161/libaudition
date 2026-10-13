#pragma once

#include <audition/audio/recording_timeline_json.hpp>
#include <audition/speech/per_source_speech_runtime.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace audition::recording {

// One producer owns this coordinator (the ALSA/ROS/CLI input loop).
// No Qt, ODAS, ALSA or model dependency. Persistence is independent of
// speech/classification decisions and of the capture backend.
class SourceRecordingSession final {
public:
    struct State {
        std::uint64_t track_id{0};
        RecordingTimeline timeline;
        std::filesystem::path sidecar;
        Timestamp origin{};
        bool retired{false};
        bool awaiting_asr{false};
        State(std::uint64_t track, RecordingId id, std::uint32_t rate,
              std::filesystem::path path, Timestamp capture_origin)
            : track_id(track), timeline(id, rate), sidecar(std::move(path)),
              origin(capture_origin) {}
    };

    // Call for the first written (or intentionally zero-filled) PCM sample.
    void begin(std::uint64_t track_id, RecordingId recording_id,
               const std::filesystem::path& wav, std::uint32_t sample_rate,
               Timestamp capture_origin) {
        if (!recording_id || active_.count(track_id))
            throw std::invalid_argument("Recording already active or invalid ID");
        const auto inserted = recordings_.emplace(
            std::piecewise_construct, std::forward_as_tuple(recording_id),
            std::forward_as_tuple(track_id, recording_id, sample_rate,
                                   std::filesystem::path{wav.string() + ".json"},
                                   capture_origin)).second;
        if (!inserted) throw std::invalid_argument("Duplicate recording ID");
        active_[track_id] = recording_id;
    }
    bool active(std::uint64_t track_id) const noexcept {
        return active_.count(track_id) != 0;
    }
    std::optional<RecordingId> recordingId(std::uint64_t track_id) const {
        const auto it = active_.find(track_id);
        if (it == active_.end()) return std::nullopt;
        return it->second;
    }
    void bindAsr(std::uint64_t track_id, std::uint64_t generation) {
        const auto id = recordingId(track_id);
        if (!id) return;
        const auto found = generations_.find(generation);
        if (found != generations_.end() && found->second != *id)
            throw std::invalid_argument("ASR generation assigned twice");
        generations_[generation] = *id;
    }
    void addSpeech(const SourceSpeechResult& result) {
        const auto gen = generations_.find(result.generation);
        if (gen == generations_.end()) return;
        const auto recording = recordings_.find(gen->second);
        if (recording == recordings_.end()) return;
        if (result.transcript.text.empty()) return;
        const auto begin = offset(result.start_time, recording->second);
        const auto end = offset(result.end_time, recording->second);
        if (!begin || !end) return;
        recording->second.timeline.add(TimelineSpeechEvent{
            gen->second, *begin, std::max(*begin, *end),
            result.transcript.text, result.transcript.language});
    }
    void addSound(std::uint64_t track_id, std::uint64_t start,
                  std::uint64_t end, std::string label, std::string model,
                  double probability, std::string stage) {
        const auto id = recordingId(track_id);
        if (!id || label.empty() || end < start) return;
        const auto key = std::make_tuple(*id, stage, end);
        if (seen_windows_.count(key)) return;
        recordings_.at(*id).timeline.add(TimelineSoundEvent{
            *id, start, end, std::move(label), std::move(model),
            probability, std::move(stage)});
        seen_windows_.insert(key);
    }
    void addGap(std::uint64_t track_id, std::uint64_t start,
                std::uint64_t samples) {
        const auto id = recordingId(track_id);
        if (!id || !samples) return;
        recordings_.at(*id).timeline.add(TimelineGapEvent{
            *id, start, start + samples, "recorder_backpressure"});
    }
    // Non-blocking; only retired generations retain their timeline.
    void retire(std::uint64_t track_id, std::optional<std::uint64_t> generation) {
        const auto current = active_.find(track_id);
        if (current == active_.end()) return;
        const auto id = current->second;
        active_.erase(current);
        const auto it = recordings_.find(id);
        it->second.retired = true;
        if (generation) {
            generations_[*generation] = id;
            it->second.awaiting_asr = true;
            return;
        }
        persistAndErase(id);
    }
    void completeGeneration(std::uint64_t generation) {
        const auto gen = generations_.find(generation);
        if (gen == generations_.end()) return;
        const auto id = gen->second;
        generations_.erase(gen);
        const auto recording = recordings_.find(id);
        if (recording != recordings_.end() && recording->second.retired)
            persistAndErase(id);
    }
    // Snapshot all data even when a model has not finished by shutdown deadline.
    void flushAll() {
        for (const auto& entry : recordings_) persist(entry.second);
    }
    [[nodiscard]] const std::optional<std::string>& persistenceError() const noexcept {
        return last_persistence_error_;
    }
    std::size_t pendingRetirements() const noexcept {
        std::size_t n = 0;
        for (const auto& entry : recordings_)
            if (entry.second.retired && entry.second.awaiting_asr) ++n;
        return n;
    }
    std::size_t activeRecordings() const noexcept { return active_.size(); }

private:
    static std::optional<std::uint64_t> offset(Timestamp timestamp,
                                                const State& record) {
        if (!timestamp.comparableWith(record.origin)) return std::nullopt;
        if (timestamp.nanoseconds() <= record.origin.nanoseconds()) return 0;
        const auto delta = static_cast<long double>(
            timestamp.nanoseconds() - record.origin.nanoseconds());
        return static_cast<std::uint64_t>(
            delta * static_cast<long double>(record.timeline.sampleRateHz()) /
            1000000000.0L);
    }
    bool persist(const State& record) {
        try {
            writeRecordingTimelineJson(record.sidecar, record.timeline);
            return true;
        } catch (const std::exception& e) {
            // Persisting annotations must not kill capture on full/unwritable disk.
            last_persistence_error_ = e.what();
            return false;
        }
    }
    void persistAndErase(RecordingId id) {
        auto it = recordings_.find(id);
        if (it == recordings_.end()) return;
        // Bound session metadata even when disk is unwritable; report error.
        persist(it->second);
        recordings_.erase(it);
        for (auto seen = seen_windows_.begin(); seen != seen_windows_.end();) {
            if (std::get<0>(*seen) == id) seen = seen_windows_.erase(seen);
            else ++seen;
        }
    }
    std::optional<std::string> last_persistence_error_{};
    std::map<std::uint64_t, RecordingId> active_;
    std::map<RecordingId, State> recordings_;
    std::map<std::uint64_t, RecordingId> generations_;
    std::set<std::tuple<RecordingId, std::string, std::uint64_t>> seen_windows_;
};

} // namespace audition::recording
