#include <audition/speech/source_speech_runtime.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace audition {
namespace {
std::size_t frames(std::uint32_t sample_rate, std::size_t milliseconds) {
    return static_cast<std::size_t>(sample_rate) * milliseconds / 1000U;
}
Duration offsetTime(std::uint64_t samples, std::uint32_t sample_rate) {
    return Duration{static_cast<std::int64_t>(
        samples * 1000000000ULL / static_cast<std::uint64_t>(sample_rate))};
}
bool allowsRate(const AudioRequirements& requirements, std::uint32_t rate) {
    const auto& rates = requirements.supported_sample_rates_hz;
    return (rates.empty() ||
            std::find(rates.begin(), rates.end(), rate) != rates.end()) &&
        requirements.min_channels <= 1U && requirements.max_channels >= 1U &&
        std::find(requirements.supported_layouts.begin(),
                  requirements.supported_layouts.end(),
                  AudioLayout::Interleaved) != requirements.supported_layouts.end();
}
} // namespace

struct SourceSpeechRuntime::SourceState {
    std::uint64_t generation{0U};
    std::unique_ptr<IVadSession> vad{};
    Timestamp origin{};
    std::uint64_t next_sample{0U};
    std::vector<float> partial{};
    std::deque<float> pre_roll{};
    std::vector<float> speech{};
    std::uint64_t speech_start_sample{0U};
    std::size_t voiced_samples{0U};
    std::size_t silence_samples{0U};
    bool active{false};
};

SourceSpeechRuntime::SourceSpeechRuntime(
    std::shared_ptr<IVoiceActivityDetector> vad,
    std::shared_ptr<IAsrEngine> asr, SourceSpeechOptions options)
    : vad_(std::move(vad)), asr_(std::move(asr)), options_(options) {
    if (!vad_ || !asr_ || options_.sample_rate_hz == 0U ||
        options_.max_sources == 0U || options_.max_queued_chunks == 0U ||
        options_.max_queued_segments == 0U || options_.max_events == 0U ||
        options_.max_segment_ms > 120000U || options_.max_segment_ms < 500U ||
        options_.pre_roll_ms >= options_.max_segment_ms ||
        options_.min_speech_ms == 0U ||
        options_.min_speech_ms >= options_.max_segment_ms ||
        options_.post_roll_ms >= options_.max_segment_ms) {
        throw std::invalid_argument{"Invalid per-source VAD/ASR configuration"};
    }
    const auto req = vad_->audioRequirements();
    if (!allowsRate(req, options_.sample_rate_hz) ||
        !allowsRate(asr_->capabilities().audio, options_.sample_rate_hz)) {
        throw std::invalid_argument{"VAD and ASR must accept mono PCM at the chosen rate"};
    }
    vad_frames_ = options_.vad_frame_samples != 0U
        ? options_.vad_frame_samples : req.preferred_frame_count.value_or(512U);
    if (vad_frames_ == 0U ||
        vad_frames_ > frames(options_.sample_rate_hz, options_.max_segment_ms)) {
        throw std::invalid_argument{"Invalid fixed VAD frame size"};
    }
    vad_thread_ = std::thread{[this] { vadLoop(); }};
    try {
        asr_thread_ = std::thread{[this] { asrLoop(); }};
    } catch (...) {
        { std::lock_guard<std::mutex> lock{mutex_}; stopping_ = true; }
        vad_cv_.notify_all();
        vad_thread_.join();
        throw;
    }
}

SourceSpeechRuntime::~SourceSpeechRuntime() {
    {
        std::lock_guard<std::mutex> lock{mutex_};
        stopping_ = true;
        pending_chunks_.clear();
        pending_segments_.clear();
    }
    vad_cv_.notify_all();
    asr_cv_.notify_all();
    if (vad_thread_.joinable()) vad_thread_.join();
    if (asr_thread_.joinable()) asr_thread_.join();
}

bool SourceSpeechRuntime::currentLocked(
    std::uint64_t source_id, std::uint64_t generation) const {
    const auto it = generations_.find(source_id);
    return it != generations_.end() && it->second == generation;
}

void SourceSpeechRuntime::invalidateLocked(
    std::uint64_t source_id, bool drop_status) {
    // Unique generations prevent a late ASR result from matching a reused
    // numeric ID. Remove retired identities to bound long-running memory.
    if (drop_status) generations_.erase(source_id);
    else if (auto it = generations_.find(source_id); it != generations_.end()) {
        it->second = next_generation_++;
    }
    pending_chunks_.erase(std::remove_if(pending_chunks_.begin(), pending_chunks_.end(),
        [source_id](const Chunk& x) { return x.source_id == source_id; }),
        pending_chunks_.end());
    pending_segments_.erase(std::remove_if(pending_segments_.begin(), pending_segments_.end(),
        [source_id](const SegmentJob& x) { return x.source_id == source_id; }),
        pending_segments_.end());
    events_.erase(std::remove_if(events_.begin(), events_.end(),
        [source_id](const SourceTranscriptEvent& x) { return x.source_id == source_id; }),
        events_.end());
    if (drop_status) {
        statuses_.erase(source_id);
    } else if (auto status = statuses_.find(source_id); status != statuses_.end()) {
        status->second.speech_active = false;
        status->second.last_transcript.reset();
    }
}

bool SourceSpeechRuntime::push(std::uint64_t source_id, AudioView audio) {
    if (audio.format().sample_rate_hz != options_.sample_rate_hz ||
        audio.format().channel_count != 1U ||
        audio.format().layout != AudioLayout::Interleaved ||
        (audio.sampleCount() != 0U && audio.data() == nullptr) ||
        audio.sampleCount() > static_cast<std::size_t>(options_.sample_rate_hz)) {
        throw std::invalid_argument{"Speech input requires mono interleaved chunks <=1 second at configured rate"};
    }
    if (audio.sampleCount() == 0U) return true;
    // Perform validation before queuing anything, with no silent NaNs.
    for (std::size_t i = 0; i < audio.sampleCount(); ++i) {
        if (!std::isfinite(audio.data()[i])) {
            throw std::invalid_argument{"Nonfinite source audio sample"};
        }
    }
    std::lock_guard<std::mutex> lock{mutex_};
    if (stopping_) return false;
    if (!statuses_.count(source_id)) {
        if (statuses_.size() >= options_.max_sources) return false;
        statuses_.emplace(source_id, SourceSpeechStatus{});
        if (!generations_.count(source_id)) {
            generations_[source_id] = next_generation_++;
        }
    }
    auto& status = statuses_.at(source_id);
    if (!status.error.empty()) return false;
    if (pending_chunks_.size() >= options_.max_queued_chunks) {
        invalidateLocked(source_id, false);
        ++status.dropped_chunks;
        return false;
    }
    const auto generation = generations_.at(source_id);
    pending_chunks_.push_back({source_id, generation, audio.captureTime(),
                               {audio.data(), audio.data() + audio.sampleCount()}});
    status.received_samples += audio.sampleCount();
    vad_cv_.notify_one();
    return true;
}

void SourceSpeechRuntime::forget(std::uint64_t source_id) {
    std::lock_guard<std::mutex> lock{mutex_};
    invalidateLocked(source_id, true);
}

SourceSpeechStatus SourceSpeechRuntime::status(std::uint64_t source_id) const {
    std::lock_guard<std::mutex> lock{mutex_};
    if (const auto it = statuses_.find(source_id); it != statuses_.end())
        return it->second;
    return {};
}

std::vector<SourceTranscriptEvent> SourceSpeechRuntime::drainTranscripts() {
    std::lock_guard<std::mutex> lock{mutex_};
    std::vector<SourceTranscriptEvent> result;
    result.reserve(events_.size());
    for (auto& event : events_) result.push_back(std::move(event));
    events_.clear();
    return result;
}

void SourceSpeechRuntime::queueSegment(
    std::uint64_t source_id, std::uint64_t generation, SourceState& state) {
    const auto end = state.next_sample;
    const auto start = state.speech_start_sample;
    SpeechSegment segment{};
    segment.track_id = SpatialTrackId{source_id};
    segment.audio = AudioBuffer{
        std::move(state.speech),
        {options_.sample_rate_hz, 1U, AudioLayout::Interleaved},
        state.origin.advancedBy(offsetTime(start, options_.sample_rate_hz))};
    std::lock_guard<std::mutex> lock{mutex_};
    if (!currentLocked(source_id, generation)) return;
    if (pending_segments_.size() >= options_.max_queued_segments) {
        ++statuses_[source_id].dropped_segments;
        return;
    }
    segment.segment_id = SpeechSegmentId{next_segment_id_++};
    pending_segments_.push_back({source_id, generation, start, end, std::move(segment)});
    asr_cv_.notify_one();
}

void SourceSpeechRuntime::handleChunk(
    const Chunk& chunk,
    std::unordered_map<std::uint64_t, std::unique_ptr<SourceState>>& states) {
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!currentLocked(chunk.source_id, chunk.generation)) return;
    }
    auto& ptr = states[chunk.source_id];
    if (!ptr || ptr->generation != chunk.generation) {
        auto fresh = std::make_unique<SourceState>();
        fresh->generation = chunk.generation;
        fresh->origin = chunk.capture_time;
        fresh->vad = vad_->createSession();
        if (!fresh->vad) throw std::runtime_error{"VAD backend returned null session"};
        ptr = std::move(fresh);
    }
    auto& state = *ptr;
    state.partial.insert(state.partial.end(), chunk.samples.begin(), chunk.samples.end());
    const auto preroll_samples = frames(options_.sample_rate_hz, options_.pre_roll_ms);
    const auto postroll_samples = frames(options_.sample_rate_hz, options_.post_roll_ms);
    const auto max_samples = frames(options_.sample_rate_hz, options_.max_segment_ms);
    const auto min_samples = frames(options_.sample_rate_hz, options_.min_speech_ms);
    while (state.partial.size() >= vad_frames_) {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            if (!currentLocked(chunk.source_id, chunk.generation)) return;
        }
        const auto at = state.next_sample;
        const AudioView view{state.partial.data(), vad_frames_,
            {options_.sample_rate_hz, 1U, AudioLayout::Interleaved},
            state.origin.advancedBy(offsetTime(at, options_.sample_rate_hz))};
        const auto vad_result = state.vad->process(view);
        const auto is_voice = vad_result.speech_active;
        state.next_sample += vad_frames_;
        if (!state.active) {
            if (is_voice) {
                state.active = true;
                state.voiced_samples = vad_frames_;
                state.silence_samples = 0U;
                state.speech_start_sample = at - static_cast<std::uint64_t>(state.pre_roll.size());
                state.speech.assign(state.pre_roll.begin(), state.pre_roll.end());
                state.pre_roll.clear();
                state.speech.insert(state.speech.end(),
                                    state.partial.begin(), state.partial.begin() +
                                    static_cast<std::ptrdiff_t>(vad_frames_));
            } else {
                state.pre_roll.insert(state.pre_roll.end(), state.partial.begin(),
                                      state.partial.begin() + static_cast<std::ptrdiff_t>(vad_frames_));
                while (state.pre_roll.size() > preroll_samples) state.pre_roll.pop_front();
            }
        } else {
            state.speech.insert(state.speech.end(), state.partial.begin(),
                                state.partial.begin() + static_cast<std::ptrdiff_t>(vad_frames_));
            if (is_voice) {
                state.voiced_samples += vad_frames_;
                state.silence_samples = 0U;
            } else {
                state.silence_samples += vad_frames_;
            }
            if ((!is_voice && state.silence_samples >= postroll_samples) ||
                state.speech.size() >= max_samples) {
                const auto keep_pre = std::min(preroll_samples, state.speech.size());
                state.pre_roll.assign(state.speech.end() -
                    static_cast<std::ptrdiff_t>(keep_pre), state.speech.end());
                if (state.voiced_samples >= min_samples) {
                    queueSegment(chunk.source_id, chunk.generation, state);
                }
                state.speech.clear();
                state.active = false;
                state.voiced_samples = 0U;
                state.silence_samples = 0U;
            }
        }
        state.partial.erase(state.partial.begin(),
                            state.partial.begin() + static_cast<std::ptrdiff_t>(vad_frames_));
        std::lock_guard<std::mutex> lock{mutex_};
        if (!currentLocked(chunk.source_id, chunk.generation)) return;
        statuses_[chunk.source_id].speech_active = state.active;
    }
}

void SourceSpeechRuntime::vadLoop() noexcept {
    std::unordered_map<std::uint64_t, std::unique_ptr<SourceState>> states;
    for (;;) {
        Chunk chunk;
        {
            std::unique_lock<std::mutex> lock{mutex_};
            vad_cv_.wait(lock, [this] { return stopping_ || !pending_chunks_.empty(); });
            if (stopping_) break;
            chunk = std::move(pending_chunks_.front());
            pending_chunks_.pop_front();
        }
        try {
            handleChunk(chunk, states);
        // Retired sessions are worker-owned and lazily reclaimed. This
        // avoids retaining every ODAS ID ever observed in a long capture.
        if (states.size() > 2U * options_.max_sources) {
            std::lock_guard<std::mutex> lock{mutex_};
            for (auto it = states.begin(); it != states.end();) {
                if (!currentLocked(it->first, it->second->generation)) {
                    it = states.erase(it);
                } else {
                    ++it;
                }
            }
        }
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock{mutex_};
            if (currentLocked(chunk.source_id, chunk.generation)) {
                statuses_[chunk.source_id].error = error.what();
                invalidateLocked(chunk.source_id, false);
                statuses_[chunk.source_id].error = error.what();
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock{mutex_};
            if (currentLocked(chunk.source_id, chunk.generation)) {
                invalidateLocked(chunk.source_id, false);
                statuses_[chunk.source_id].error = "Unknown VAD failure";
            }
        }
    }
}

void SourceSpeechRuntime::asrLoop() noexcept {
    for (;;) {
        SegmentJob job;
        {
            std::unique_lock<std::mutex> lock{mutex_};
            asr_cv_.wait(lock, [this] { return stopping_ || !pending_segments_.empty(); });
            if (stopping_) break;
            job = std::move(pending_segments_.front());
            pending_segments_.pop_front();
            if (!currentLocked(job.source_id, job.generation)) continue;
        }
        try {
            Transcript transcript = asr_->transcribe(job.segment);
            transcript.segment_id = job.segment.segment_id;
            std::lock_guard<std::mutex> lock{mutex_};
            if (!currentLocked(job.source_id, job.generation)) continue;
            auto& status = statuses_[job.source_id];
            status.completed_segments++;
            status.last_transcript = transcript;
            if (events_.size() >= options_.max_events) events_.pop_front();
            events_.push_back({job.source_id, job.generation,
                               job.start_sample, job.end_sample,
                               std::move(transcript)});
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock{mutex_};
            if (currentLocked(job.source_id, job.generation)) {
                ++statuses_[job.source_id].dropped_segments;
                statuses_[job.source_id].error = error.what();
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock{mutex_};
            if (currentLocked(job.source_id, job.generation)) {
                ++statuses_[job.source_id].dropped_segments;
                statuses_[job.source_id].error = "Unknown ASR failure";
            }
        }
    }
}
} // namespace audition
