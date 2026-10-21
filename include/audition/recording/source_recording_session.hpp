#pragma once

#include <audition/audio/recording_timeline_json.hpp>
#include <audition/speech/per_source_speech_runtime.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
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
struct SourceRecordingOptions {
    std::size_t max_recordings{64};
    std::size_t max_speech_events{4096};
    std::size_t max_sound_events{8192};
    std::size_t max_gap_events{4096};
    // Total retained speech and classifier text bytes per recording.
    std::size_t max_annotation_text_bytes{1048576};
};
struct SourceRecordingDrops {
    std::uint64_t speech{0};
    std::uint64_t sound{0};
    std::uint64_t gaps{0};
};
class SourceRecordingSession final {
public:
    SourceRecordingSession() = default;
    explicit SourceRecordingSession(SourceRecordingOptions options)
        : options_(options) {
        if (!options_.max_recordings || !options_.max_speech_events ||
            !options_.max_sound_events || !options_.max_gap_events ||
            !options_.max_annotation_text_bytes)
            throw std::invalid_argument("Recording annotation limits must be positive");
    }
    struct State {
        std::uint64_t track_id{0};
        RecordingTimeline timeline;
        std::filesystem::path sidecar;
        Timestamp origin{};
        bool retired{false};
        bool awaiting_asr{false};
        bool pending_retire_ack{false};
        std::size_t annotation_text_bytes{0};
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
        if (recordings_.size() >= options_.max_recordings)
            throw std::length_error("Recording session capacity exceeded");
        const auto inserted = recordings_.emplace(
            std::piecewise_construct, std::forward_as_tuple(recording_id),
            std::forward_as_tuple(track_id, recording_id, sample_rate,
                                   std::filesystem::path{wav.string() + ".json"},
                                   capture_origin)).second;
        if (!inserted) throw std::invalid_argument("Duplicate recording ID");
        active_[track_id] = recording_id;
    }
    // Full recording metadata is normal backpressure, not a capture failure.
    // Invalid/duplicate identities remain exceptions as in begin().
    bool tryBegin(std::uint64_t track_id, RecordingId recording_id,
                  const std::filesystem::path& wav, std::uint32_t sample_rate,
                  Timestamp capture_origin) {
        if (!recording_id || active_.count(track_id))
            throw std::invalid_argument("Recording already active or invalid ID");
        if (recordings_.size() >= options_.max_recordings) return false;
        begin(track_id, recording_id, wav, sample_rate, capture_origin);
        return true;
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
        if (id) bindRecordingAsr(*id, generation);
    }
    // Bind the exact WAV identity, even if its numeric source ID was
    // already retired/reused before asynchronous VAD created a generation.
    void bindRecordingAsr(RecordingId id, std::uint64_t generation) {
        if (!recordings_.count(id))
            throw std::invalid_argument("Cannot bind ASR to unknown recording");
        const auto found = generations_.find(generation);
        if (found != generations_.end() && found->second != id)
            throw std::invalid_argument("ASR generation assigned twice");
        generations_[generation] = id;
    }
    // A producer discontinuity cancels the old speech generation while its
    // WAV may remain open. Do not retain stale generation-to-WAV links.
    void cancelGeneration(std::uint64_t generation) {
        generations_.erase(generation);
    }
    void addSpeech(const SourceSpeechResult& result) {
        const auto gen = generations_.find(result.generation);
        if (gen == generations_.end()) return;
        const auto recording = recordings_.find(gen->second);
        if (recording == recordings_.end()) return;
        if (result.transcript.text.empty()) return;
        const auto& state = recording->second;
        if (state.timeline.speech().size() >= options_.max_speech_events ||
            !fitsTextBudget(state, {&result.transcript.text,
                                    &result.transcript.language})) {
            ++drops_.speech;
            return;
        }
        const auto begin = offset(result.start_time, recording->second);
        const auto end = offset(result.end_time, recording->second);
        if (!begin || !end) return;
        recording->second.timeline.add(TimelineSpeechEvent{
            gen->second, *begin, std::max(*begin, *end),
            result.transcript.text, result.transcript.language});
        recording->second.annotation_text_bytes +=
            result.transcript.text.size() + result.transcript.language.size();
    }
    void addSound(std::uint64_t track_id, std::uint64_t start,
                  std::uint64_t end, std::string label, std::string model,
                  double probability, std::string stage) {
        const auto id = recordingId(track_id);
        if (!id || label.empty() || end < start) return;
        // Invalid model scores must not throw into the recording/capture
        // owner thread. Preserve the strict RecordingTimeline invariant.
        if (!std::isfinite(probability) || probability < 0.0 ||
            probability > 1.0) {
            ++drops_.sound;
            return;
        }
        // Suppress only an exact class from one model/stage/window.
        // Multilabel classifiers can legitimately emit several classes
        // over the same time interval.
        const auto key = std::make_tuple(*id, model, stage, start, end, label);
        if (seen_windows_.count(key)) return;
        auto& state = recordings_.at(*id);
        if (state.timeline.sounds().size() >= options_.max_sound_events ||
            !fitsTextBudget(state, {&label, &model, &stage})) {
            ++drops_.sound;
            return;
        }
        const auto text_bytes = label.size() + model.size() + stage.size();
        state.timeline.add(TimelineSoundEvent{
            *id, start, end, std::move(label), std::move(model),
            probability, std::move(stage)});
        state.annotation_text_bytes += text_bytes;
        seen_windows_.insert(key);
    }
    void addGap(std::uint64_t track_id, std::uint64_t start,
                std::uint64_t samples) {
        const auto id = recordingId(track_id);
        if (!id || !samples) return;
        if (samples > std::numeric_limits<std::uint64_t>::max() - start)
            throw std::length_error("Recording gap sample offset overflow");
        auto& state = recordings_.at(*id);
        const auto& gaps = state.timeline.gaps();
        // Adjacent gaps merge without consuming a second event slot.
        const bool merges = !gaps.empty() && gaps.back().end_sample == start &&
                            gaps.back().reason == "recorder_backpressure";
        if (!merges && gaps.size() >= options_.max_gap_events) {
            ++drops_.gaps;
            return;
        }
        state.timeline.add(TimelineGapEvent{
            *id, start, start + samples, "recorder_backpressure"});
    }
    // Retire the producer-side WAV before the queued VAD worker has
    // processed final PCM. A delayed acknowledgement must be matched by
    // RecordingId, never by a reused numeric ODAS track ID.
    [[nodiscard]] std::optional<RecordingId> retireDeferred(std::uint64_t track_id) {
        const auto active = active_.find(track_id);
        if (active == active_.end()) return std::nullopt;
        const auto id = active->second;
        active_.erase(active);
        auto& state = recordings_.at(id);
        state.retired = true;
        state.awaiting_asr = true;
        state.pending_retire_ack = true;
        return id;
    }

    // Complete a deferred retirement after the serialized speech worker
    // reports the generation. A null generation persists a WAV-only sidecar.
    void acknowledgeRetired(RecordingId id, std::optional<std::uint64_t> generation) {
        const auto record = recordings_.find(id);
        if (record == recordings_.end() || !record->second.pending_retire_ack)
            throw std::invalid_argument("Unknown deferred recording retirement");
        if (generation) {
            bindRecordingAsr(id,*generation); // validate before state mutation
            record->second.pending_retire_ack = false;
        } else {
            persistAndErase(id);
        }
    }

    // Non-blocking; only retired generations retain their timeline.
    void retire(std::uint64_t track_id, std::optional<std::uint64_t> generation) {
        const auto current = active_.find(track_id);
        if (current == active_.end()) return;
        const auto id = current->second;
        // A generation must never be reassigned from another recording:
        // check before retiring the active track or changing its timeline.
        if (generation) {
            const auto bound = generations_.find(*generation);
            if (bound != generations_.end() && bound->second != id)
                throw std::invalid_argument("ASR generation assigned twice");
        }
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
    // Active and retired generation bindings still held in this session.
    std::size_t boundAsrGenerations() const noexcept { return generations_.size(); }
    std::size_t retainedRecordings() const noexcept { return recordings_.size(); }
    SourceRecordingDrops droppedAnnotations() const noexcept { return drops_; }

private:
    bool fitsTextBudget(const State& state,
                        std::initializer_list<const std::string*> values) const noexcept {
        std::size_t remaining = options_.max_annotation_text_bytes -
                                state.annotation_text_bytes;
        for (const auto* value : values) {
            if (value->size() > remaining) return false;
            remaining -= value->size();
        }
        return true;
    }
    static std::optional<std::uint64_t> offset(Timestamp timestamp,
                                                const State& record) {
        if (!timestamp.comparableWith(record.origin)) return std::nullopt;
        if (timestamp.nanoseconds() <= record.origin.nanoseconds()) return 0;
        // Unsigned subtraction computes the exact nonnegative difference
        // even when the signed nanosecond delta exceeds INT64_MAX.
        const auto delta = static_cast<std::uint64_t>(timestamp.nanoseconds()) -
                           static_cast<std::uint64_t>(record.origin.nanoseconds());
        constexpr std::uint64_t billion = 1000000000ULL;
        const auto whole_seconds = delta / billion;
        const auto fractional_ns = delta % billion;
        const auto rate = static_cast<std::uint64_t>(record.timeline.sampleRateHz());
        const auto max_samples = std::numeric_limits<std::uint64_t>::max();
        if (whole_seconds > max_samples / rate) return std::nullopt;
        const auto whole_samples = whole_seconds * rate;
        // fractional_ns < 1e9 and rate <= UINT32_MAX: product fits uint64.
        const auto fraction_samples = (fractional_ns * rate) / billion;
        if (fraction_samples > max_samples - whole_samples) return std::nullopt;
        return whole_samples + fraction_samples;
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
        // An ASR generation may have been bound before a fallback retire
        // without generation (speech retirement table at capacity). Remove
        // every binding for this now-persisted recording to avoid leakage.
        for (auto gen = generations_.begin(); gen != generations_.end();) {
            if (gen->second == id) gen = generations_.erase(gen);
            else ++gen;
        }
        for (auto seen = seen_windows_.begin(); seen != seen_windows_.end();) {
            if (std::get<0>(*seen) == id) seen = seen_windows_.erase(seen);
            else ++seen;
        }
    }
    SourceRecordingOptions options_{};
    SourceRecordingDrops drops_{};
    std::optional<std::string> last_persistence_error_{};
    std::map<std::uint64_t, RecordingId> active_;
    std::map<RecordingId, State> recordings_;
    std::map<std::uint64_t, RecordingId> generations_;
    std::set<std::tuple<RecordingId, std::string, std::string,
                        std::uint64_t, std::uint64_t, std::string>> seen_windows_;
};

} // namespace audition::recording
