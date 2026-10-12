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
            ++pending_by_generation_[track.generation];
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
        try {
            track.vad = vad_.createSession();
            if (!track.vad) track.error = "VAD returned null session";
        } catch (const std::exception& e) {
            track.error = e.what();
        } catch (...) {
            track.error = "Unknown VAD session initialization failure";
        }
        if (track.error) {
            error_ = "Track " + std::to_string(id) + ": " + *track.error;
            ++dropped_;
        }
        track.generation = next_generation_++;
        it = tracks_.emplace(id, std::move(track)).first;
    }
    auto& track = it->second;
    // A failed source must be explicitly forgotten before its ID is reused.
    // Do not let one malformed ODAS frame poison all other capture tracks.
    if (track.error) return;
    for (std::size_t i = 0; i < audio.sampleCount(); ++i) {
        if (!std::isfinite(audio.data()[i])) {
            track.error = "Nonfinite separated source PCM";
            error_ = "Track " + std::to_string(id) + ": " + *track.error;
            track.speech.clear();
            track.pre_roll.clear();
            track.speech_active = false;
            track.voiced_samples = 0U;
            ++dropped_;
            return;
        }
    }

    bool active = false;
    try {
        active = track.vad->process(audio).speech_active;
    } catch (const std::exception& e) {
        track.error = e.what();
    } catch (...) {
        track.error = "Unknown per-source VAD exception";
    }
    if (track.error) {
        error_ = "Track " + std::to_string(id) + ": " + *track.error;
        track.speech.clear();
        track.pre_roll.clear();
        track.speech_active = false;
        track.voiced_samples = 0U;
        ++dropped_;
        return;
    }
    track.speech_active = active;
    const auto advanceTime = [&](std::size_t sample_offset) {
        const auto ns = static_cast<std::int64_t>(
            static_cast<double>(sample_offset) * 1000000000.0 /
            static_cast<double>(options_.sample_rate_hz));
        return audio.captureTime().advancedBy(Duration{ns});
    };
    const auto rememberSilence = [&](std::size_t first_sample) {
        for (std::size_t i = first_sample; i < audio.sampleCount(); ++i) {
            track.pre_roll.push_back(audio.data()[i]);
            if (track.pre_roll.size() > options_.pre_roll_samples)
                track.pre_roll.pop_front();
        }
    };

    if (active) {
        // Model VAD is chunk-based. It declares the entire chunk speech-active;
        // retain pre-roll so a late detector does not chop the first syllable.
        if (track.speech.empty()) {
            const auto pre_ns = static_cast<std::int64_t>(
                static_cast<double>(track.pre_roll.size()) * 1000000000.0 /
                static_cast<double>(options_.sample_rate_hz));
            track.start_time = audio.captureTime().advancedBy(Duration{-pre_ns});
            track.speech.insert(track.speech.end(),
                track.pre_roll.begin(), track.pre_roll.end());
            track.pre_roll.clear();
        }
        track.trailing_silence_samples = 0U;
        std::size_t offset = 0U;
        while (offset < audio.sampleCount()) {
            const auto available = options_.max_segment_samples - track.speech.size();
            const auto take = std::min(available, audio.sampleCount() - offset);
            track.speech.insert(track.speech.end(), audio.data() + offset,
                                audio.data() + offset + take);
            track.voiced_samples += take;
            offset += take;
            if (track.speech.size() == options_.max_segment_samples) {
                queueSegment(id, track);
                // Continue in the same capture chunk; no samples may be
                // dropped at a max-duration boundary.
                track.start_time = advanceTime(offset);
                track.trailing_silence_samples = 0U;
            }
        }
    } else if (!track.speech.empty()) {
        if (options_.post_roll_samples == 0U) {
            queueSegment(id, track);
            track.trailing_silence_samples = 0U;
            rememberSilence(0U);
        } else {
            const auto missing = options_.post_roll_samples -
                std::min(options_.post_roll_samples, track.trailing_silence_samples);
            const auto available = options_.max_segment_samples - track.speech.size();
            const auto take = std::min({audio.sampleCount(), missing, available});
            track.speech.insert(track.speech.end(), audio.data(), audio.data() + take);
            track.trailing_silence_samples += take;
            if (track.speech.size() == options_.max_segment_samples ||
                track.trailing_silence_samples >= options_.post_roll_samples) {
                queueSegment(id, track);
                track.trailing_silence_samples = 0U;
                rememberSilence(take);
            }
        }
    } else {
        rememberSilence(0U);
    }
}

std::optional<std::uint64_t> PerSourceSpeechRuntime::retire(std::uint64_t id) {
    std::lock_guard<std::mutex> guard{mutex_};
    auto it = tracks_.find(id);
    if (it == tracks_.end()) return std::nullopt;
    const auto generation = it->second.generation;
    if (!it->second.speech.empty()) queueSegment(id, it->second);
    retired_generations_.insert(generation);
    tracks_.erase(it);
    return generation;
}

std::size_t PerSourceSpeechRuntime::pendingResults(std::uint64_t generation) const {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto it = pending_by_generation_.find(generation);
    return it == pending_by_generation_.end() ? 0U : it->second;
}

void PerSourceSpeechRuntime::releaseRetired(std::uint64_t generation) {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto pending = pending_by_generation_.find(generation);
    if (pending != pending_by_generation_.end() && pending->second != 0)
        throw std::logic_error("Cannot release in-flight ASR generation");
    pending_by_generation_.erase(generation);
    retired_generations_.erase(generation);
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
    for (auto it = pending_by_generation_.begin(); it != pending_by_generation_.end();) {
        if (it->second == 0) it = pending_by_generation_.erase(it);
        else ++it;
    }
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
    retired_generations_.clear();
    pending_by_generation_.clear();
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

SourceSpeechTrackStatus PerSourceSpeechRuntime::trackStatus(
    std::uint64_t track_id) const {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto it = tracks_.find(track_id);
    if (it == tracks_.end()) return {};
    return {true, it->second.speech_active, it->second.speech.size(),
            it->second.voiced_samples, it->second.error};
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
            if ((current != tracks_.end() &&
                 current->second.generation == job.generation) ||
                retired_generations_.count(job.generation) != 0) {
                results_.push_back({job.track_id, job.generation, std::move(transcript),
                    job.segment.audio.captureTime(),
                    job.segment.audio.captureTime().advancedBy(job.segment.audio.duration())});
                // Prevent unbounded accumulation when application does not poll.
                if (results_.size() > options_.max_pending_segments) {
                    results_.erase(results_.begin());
                    ++dropped_;
                }
                ++completed_;
            }
            if (auto it = pending_by_generation_.find(job.generation);
                it != pending_by_generation_.end() && it->second > 0) --it->second;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> guard{mutex_};
            error_ = e.what();
            if (auto it = pending_by_generation_.find(job.generation);
                it != pending_by_generation_.end() && it->second > 0) --it->second;
            ++dropped_;
        } catch (...) {
            std::lock_guard<std::mutex> guard{mutex_};
            error_ = "Unknown ASR exception";
            if (auto it = pending_by_generation_.find(job.generation);
                it != pending_by_generation_.end() && it->second > 0) --it->second;
            ++dropped_;
        }
    }
}

} // namespace audition
