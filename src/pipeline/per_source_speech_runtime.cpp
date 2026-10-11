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
        options_.max_segment_samples < options_.min_speech_samples) {
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
    if (track.speech.size() >= options_.min_speech_samples) {
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
        track.generation = ++generations_[id];
        it = tracks_.emplace(id, std::move(track)).first;
    }
    auto& track = it->second;
    const auto active = track.vad->process(audio).speech_active;
    if (active) {
        if (track.speech.empty()) track.start_time = audio.captureTime();
        const auto available = options_.max_segment_samples - track.speech.size();
        const auto take = std::min(available, audio.sampleCount());
        for (std::size_t i = 0; i < take; ++i) {
            if (!std::isfinite(audio.data()[i])) {
                throw std::invalid_argument{"Nonfinite separated audio"};
            }
        }
        track.speech.insert(track.speech.end(), audio.data(), audio.data() + take);
        if (track.speech.size() >= options_.max_segment_samples) {
            queueSegment(id, track);
        }
        if (take < audio.sampleCount()) ++dropped_; // intentional segment cap
    } else {
        queueSegment(id, track);
    }
}

void PerSourceSpeechRuntime::forget(std::uint64_t id) {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto it = tracks_.find(id);
    if (it != tracks_.end()) {
        queueSegment(id, it->second);
        tracks_.erase(it);
    }
    // Retiring a track cancels all pending and in-flight ASR from its generation.
    ++generations_[id];
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
    for (const auto& track : tracks_) ++generations_[track.first];
    for (const auto& job : jobs_) ++generations_[job.track_id];
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
            if (generations_[job.track_id] == job.generation) {
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
