#include <audition/speech/per_source_speech_runtime.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace audition {

PerSourceSpeechRuntime::PerSourceSpeechRuntime(
    const IVoiceActivityDetector& vad, std::shared_ptr<const IAsrEngine> asr,
    PerSourceSpeechOptions options)
    : vad_(vad), asr_(std::move(asr)), options_(options) {
    if (!asr_ || !options_.sample_rate_hz || !options_.max_sources ||
        !options_.max_pending_segments || !options_.min_speech_samples ||
        options_.max_segment_samples < options_.min_speech_samples ||
        options_.pre_roll_samples > options_.max_segment_samples ||
        options_.post_roll_samples > options_.max_segment_samples) {
        throw std::invalid_argument{"Invalid per-source speech runtime configuration"};
    }
    const auto rates = vad_.audioRequirements().supported_sample_rates_hz;
    if (!rates.empty() &&
        std::find(rates.begin(), rates.end(), options_.sample_rate_hz) == rates.end()) {
        throw std::invalid_argument{"VAD does not support configured sample rate"};
    }
    worker_ = std::thread{[this] { workerLoop(); }};
}

PerSourceSpeechRuntime::~PerSourceSpeechRuntime() {
    {
        std::lock_guard<std::mutex> guard{mutex_};
        stopping_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void PerSourceSpeechRuntime::queueSegment(std::uint64_t id, Track& track) {
    if (track.voiced_samples >= options_.min_speech_samples) {
        if (jobs_.size() >= options_.max_pending_segments) {
            ++dropped_;
        } else {
            SpeechSegment segment{};
            segment.segment_id = SpeechSegmentId{next_segment_++};
            segment.track_id = SpatialTrackId{id};
            segment.audio = AudioBuffer{std::move(track.speech),
                {options_.sample_rate_hz, 1U, AudioLayout::Interleaved},
                track.start_time};
            jobs_.push_back({id, track.generation, std::move(segment)});
            cv_.notify_one();
        }
    }
    track.speech.clear();
    track.voiced_samples = 0U;
}

void PerSourceSpeechRuntime::push(std::uint64_t id, AudioView audio) {
    if (audio.sampleCount() == 0U) return;
    if (!audio.data() || audio.format().sample_rate_hz != options_.sample_rate_hz ||
        audio.format().channel_count != 1U ||
        audio.format().layout != AudioLayout::Interleaved) {
        throw std::invalid_argument{"Per-source speech requires mono interleaved PCM"};
    }
    std::lock_guard<std::mutex> guard{mutex_};
    if (stopping_) return;
    auto it = tracks_.find(id);
    if (it == tracks_.end()) {
        if (tracks_.size() >= options_.max_sources) {
            ++dropped_;
            return;
        }
        Track track{};
        track.vad = vad_.createSession();
        if (!track.vad) throw std::runtime_error{"VAD returned null session"};
        track.generation = next_generation_++;
        it = tracks_.emplace(id, std::move(track)).first;
    }
    auto& track = it->second;
    const auto active = track.vad->process(audio).speech_active;
    for (std::size_t i = 0; i < audio.sampleCount(); ++i) {
        if (!std::isfinite(audio.data()[i])) {
            throw std::invalid_argument{"Nonfinite separated audio"};
        }
    }
    if (active) {
        if (track.speech.empty()) {
            // Preserve immediate speech onset, including recent VAD-negative
            // samples. Audio clock offset reflects the pre-roll duration.
            const auto before = static_cast<std::int64_t>(track.pre_roll.size());
            const auto delta = Duration::fromChrono(
                std::chrono::nanoseconds{before * 1000000000LL /
                                         static_cast<std::int64_t>(options_.sample_rate_hz)});
            track.start_time = audio.captureTime().advancedBy(Duration{-delta.nanoseconds()});
            track.speech.insert(track.speech.end(),
                track.pre_roll.begin(), track.pre_roll.end());
            track.pre_roll.clear();
        }
        track.trailing_silence_samples = 0U;
    }
    if (!active && !track.speech.empty() && options_.post_roll_samples == 0U) {
        // Zero post-roll means close exactly at the VAD-negative boundary.
        queueSegment(id, track);
        track.trailing_silence_samples = 0U;
    }
    if (active || !track.speech.empty()) {
        const auto available = options_.max_segment_samples - track.speech.size();
        const auto count = std::min(available, audio.sampleCount());
        track.speech.insert(track.speech.end(), audio.data(), audio.data() + count);
        if (active) track.voiced_samples += count;
        if (!active) track.trailing_silence_samples += count;
        if (count < audio.sampleCount()) ++dropped_;
        if (track.speech.size() == options_.max_segment_samples ||
            (!active && track.trailing_silence_samples >= options_.post_roll_samples)) {
            queueSegment(id, track);
            track.trailing_silence_samples = 0U;
        }
    } else if (!active && options_.pre_roll_samples != 0U) {
        for (std::size_t i = 0; i < audio.sampleCount(); ++i) {
            track.pre_roll.push_back(audio.data()[i]);
            if (track.pre_roll.size() > options_.pre_roll_samples)
                track.pre_roll.pop_front();
        }
    }
}

void PerSourceSpeechRuntime::forget(std::uint64_t id) {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto it = tracks_.find(id);
    if (it != tracks_.end()) {
        // Retiring an ODAS track invalidates unfinished audio. Do not enqueue
        // it and then immediately cancel it as this distorts loss metrics.
        tracks_.erase(it);
    }
    // Retiring a track cancels all pending and in-flight ASR from its generation.
    const auto before = jobs_.size();
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                              [id](const Job& job){return job.track_id == id;}),
                jobs_.end());
    dropped_ += before - jobs_.size();
    results_.erase(std::remove_if(results_.begin(), results_.end(),
                                  [id](const SourceSpeechResult& r){return r.track_id == id;}),
                   results_.end());
}

void PerSourceSpeechRuntime::reset() {
    std::lock_guard<std::mutex> guard{mutex_};
    dropped_ += jobs_.size();
    jobs_.clear();
    results_.clear();
    tracks_.clear();
}

std::vector<SourceSpeechResult> PerSourceSpeechRuntime::takeResults() {
    std::lock_guard<std::mutex> guard{mutex_};
    std::vector<SourceSpeechResult> out{};
    out.swap(results_);
    return out;
}

SourceSpeechStatistics PerSourceSpeechRuntime::statistics() const {
    std::lock_guard<std::mutex> guard{mutex_};
    return {completed_, dropped_, jobs_.size(), error_};
}

void PerSourceSpeechRuntime::workerLoop() {
    for (;;) {
        Job job{};
        {
            std::unique_lock<std::mutex> lock{mutex_};
            cv_.wait(lock, [this] {return stopping_ || !jobs_.empty();});
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        try {
            auto transcript = asr_->transcribe(job.segment);
            std::lock_guard<std::mutex> guard{mutex_};
            const auto current = tracks_.find(job.track_id);
            if (current != tracks_.end() &&
                current->second.generation == job.generation) {
                results_.push_back({job.track_id, job.generation, std::move(transcript)});
                // Prevent unbounded accumulation when application does not poll.
                if (results_.size() > options_.max_pending_segments) {
                    results_.erase(results_.begin());
                    ++dropped_;
                }
                ++completed_;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> guard{mutex_};
            error_ = e.what();
            ++dropped_;
        } catch (...) {
            std::lock_guard<std::mutex> guard{mutex_};
            error_ = "Unknown ASR exception";
            ++dropped_;
        }
    }
}

} // namespace audition
