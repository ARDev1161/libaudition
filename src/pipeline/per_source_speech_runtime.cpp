#include <audition/speech/per_source_speech_runtime.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace audition {
namespace {
// Subtract instead of summing untrusted lengths: avoids size_t overflow.
// VAD and ASR must agree on the mono input shape before any worker starts.
// Empty sample-rate capabilities follow the AudioRequirements any-rate contract.
bool acceptsMonoInterleaved(const AudioRequirements& requirements,
                             std::uint32_t sample_rate) {
    const auto& rates = requirements.supported_sample_rates_hz;
    return (rates.empty() ||
            std::find(rates.begin(), rates.end(), sample_rate) != rates.end()) &&
           requirements.min_channels <= 1U &&
           requirements.max_channels >= 1U &&
           std::find(requirements.supported_layouts.begin(),
                     requirements.supported_layouts.end(),
                     AudioLayout::Interleaved) != requirements.supported_layouts.end();
}
bool transcriptWithinBudget(const Transcript& transcript,
                            std::size_t bytes, std::size_t items) noexcept {
    if (transcript.tokens.size() > items ||
        transcript.words.size() > items - transcript.tokens.size()) return false;
    const auto consume = [&bytes](const std::string& value) {
        if (value.size() > bytes) return false;
        bytes -= value.size();
        return true;
    };
    if (!consume(transcript.text) || !consume(transcript.language)) return false;
    for (const auto& token : transcript.tokens)
        if (!consume(token.text)) return false;
    for (const auto& word : transcript.words)
        if (!consume(word.text)) return false;
    return true;
}
// Diagnostic only: preserve the original transcript, including hallucinations.
bool hasRepetition(const std::string& text) {
    std::istringstream words{text};
    std::vector<std::string> tokens;
    std::string word;
    while (words >> word) {
        word.erase(std::remove_if(word.begin(), word.end(),
            [](unsigned char c) { return c == ',' || c == '.' || c == '!' || c == '?'; }),
            word.end());
        if (!word.empty()) tokens.push_back(word);
    }
    for (std::size_t width = 1; width <= 4; ++width) {
        for (std::size_t start = 0; start + 4 * width <= tokens.size(); ++start) {
            bool repeated = true;
            for (std::size_t i = width; i < 4 * width; ++i) {
                if (tokens[start + i] != tokens[start + i % width]) {
                    repeated = false;
                    break;
                }
            }
            if (repeated) return true;
        }
    }
    return false;
}
} // namespace

PerSourceSpeechRuntime::PerSourceSpeechRuntime(
    const IVoiceActivityDetector& vad, std::shared_ptr<const IAsrEngine> asr,
    PerSourceSpeechOptions options)
    : vad_(vad), asr_(std::move(asr)), options_(options) {
    if (!asr_ || !options_.sample_rate_hz || !options_.max_sources ||
        !options_.max_pending_segments || !options_.max_retired_generations ||
        !options_.min_speech_samples ||
        !options_.max_result_text_bytes || !options_.max_result_items ||
        options_.max_segment_samples < options_.min_speech_samples ||
        options_.pre_roll_samples > options_.max_segment_samples ||
        options_.post_roll_samples > options_.max_segment_samples) {
        throw std::invalid_argument{"Invalid per-source speech runtime configuration"};
    }
    // Bound synchronous inspection and VAD work per call. Longer source
    // audio must be divided into contiguous chunks by the application.
    if (options_.max_input_chunk_samples == 0U)
        options_.max_input_chunk_samples = options_.sample_rate_hz;
    if (options_.max_input_chunk_samples > options_.sample_rate_hz)
        throw std::invalid_argument{"Speech input chunk limit exceeds one second"};
    if (!acceptsMonoInterleaved(vad_.audioRequirements(),
                                options_.sample_rate_hz))
        throw std::invalid_argument{"VAD does not accept mono interleaved speech PCM at configured rate"};
    if (!acceptsMonoInterleaved(asr_->capabilities().audio,
                                options_.sample_rate_hz))
        throw std::invalid_argument{"ASR does not accept mono interleaved speech PCM at configured rate"};
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

void PerSourceSpeechRuntime::queueSegment(std::uint64_t id, Track& track, const char* reason) {
    const auto segment_samples = track.speech.size();
    if (track.voiced_samples >= options_.min_speech_samples) {
        if (jobs_.size() >= options_.max_pending_segments) {
            ++dropped_;
            ++track.dropped_segments;
        } else {
            SpeechSegment segment{};
            segment.segment_id = SpeechSegmentId{next_segment_++};
            segment.track_id = SpatialTrackId{id};
            segment.audio = AudioBuffer{std::move(track.speech),
                {options_.sample_rate_hz, 1U, AudioLayout::Interleaved},
                track.start_time};
            jobs_.push_back({id, track.generation, std::move(segment),
                             track.start_sample, reason});
            ++track.queued_segments;
            ++pending_by_generation_[track.generation];
            cv_.notify_one();
        }
    }
    track.start_sample += segment_samples;
    track.speech.clear();
    track.voiced_samples = 0U;
}

void PerSourceSpeechRuntime::push(std::uint64_t id, AudioView audio) {
    if (audio.sampleCount() > options_.max_input_chunk_samples)
        throw std::length_error{"Speech PCM chunk exceeds configured input capacity"};
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
    const auto accepted = static_cast<std::uint64_t>(audio.sampleCount());
    const auto remaining = std::numeric_limits<std::uint64_t>::max() -
                           track.received_samples;
    const auto chunk_start = track.received_samples;
    track.received_samples += std::min(accepted, remaining);
    if (active && !track.speech_active) ++track.vad_starts;
    if (!active && track.speech_active) ++track.vad_ends;
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
            track.start_sample = chunk_start -
                std::min<std::uint64_t>(chunk_start, track.pre_roll.size());
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
                queueSegment(id, track, "max_duration");
                // Continue in the same capture chunk; no samples may be
                // dropped at a max-duration boundary.
                track.start_time = advanceTime(offset);
                track.start_sample = chunk_start + offset;
                track.trailing_silence_samples = 0U;
            }
        }
    } else if (!track.speech.empty()) {
        if (options_.post_roll_samples == 0U) {
            queueSegment(id, track, "vad_silence");
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
                queueSegment(id, track, track.speech.size() == options_.max_segment_samples
                    ? "max_duration" : "vad_silence");
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
    // Keep the existing source fully live if retirement metadata is full;
    // the client can drain/release generations and retry, or forget it.
    // Check BEFORE queuing its final speech or mutating the active track.
    if (retired_generations_.size() >= options_.max_retired_generations)
        return std::nullopt;
    const auto generation = it->second.generation;
    if (!it->second.speech.empty()) queueSegment(id, it->second, "retired");
    retired_generations_.insert(generation);
    tracks_.erase(it);
    return generation;
}

std::size_t PerSourceSpeechRuntime::pendingResults(std::uint64_t generation) const {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto it = pending_by_generation_.find(generation);
    return it == pending_by_generation_.end() ? 0U : it->second;
}

std::optional<std::uint64_t> PerSourceSpeechRuntime::activeGeneration(
    std::uint64_t id) const {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto it = tracks_.find(id);
    if (it == tracks_.end()) return std::nullopt;
    return it->second.generation;
}

SourceSpeechDrain PerSourceSpeechRuntime::drainReady() {
    std::lock_guard<std::mutex> guard{mutex_};
    SourceSpeechDrain drained{};
    drained.results.swap(results_);
    for (const auto generation : retired_generations_) {
        const auto pending = pending_by_generation_.find(generation);
        if (pending == pending_by_generation_.end() || pending->second == 0)
            drained.ready_retired_generations.push_back(generation);
    }
    return drained;
}

void PerSourceSpeechRuntime::completeJobLocked(std::uint64_t generation) {
    const auto pending = pending_by_generation_.find(generation);
    if (pending == pending_by_generation_.end()) return;
    if (pending->second > 0) --pending->second;
    if (pending->second == 0 && retired_generations_.count(generation) == 0)
        pending_by_generation_.erase(pending);
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
    const auto current = tracks_.find(id);
    if (current == tracks_.end()) return;
    const auto generation = current->second.generation;
    tracks_.erase(current);
    // Cancel ONLY the live generation. A previously retired generation with
    // this numeric ODAS ID must remain eligible for late ASR completion.
    const auto before = jobs_.size();
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
        [generation](const Job& job) { return job.generation == generation; }),
        jobs_.end());
    const auto cancelled = before - jobs_.size();
    dropped_ += cancelled;
    if (auto pending = pending_by_generation_.find(generation);
        pending != pending_by_generation_.end()) {
        pending->second -= std::min(pending->second, cancelled);
        if (pending->second == 0)
            pending_by_generation_.erase(pending);
    }
    results_.erase(std::remove_if(results_.begin(), results_.end(),
        [generation](const SourceSpeechResult& result) {
            return result.generation == generation;
        }), results_.end());
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
    return {completed_, dropped_, jobs_.size(), error_,
            results_.size(), evicted_results_, retired_generations_.size()};
}

SourceSpeechTrackStatus PerSourceSpeechRuntime::trackStatus(
    std::uint64_t track_id) const {
    std::lock_guard<std::mutex> guard{mutex_};
    const auto it = tracks_.find(track_id);
    if (it == tracks_.end()) return {};
    return {true, it->second.speech_active,
            it->second.vad_starts, it->second.vad_ends,
            it->second.speech.size(),
            it->second.voiced_samples, it->second.error,
            it->second.received_samples, it->second.queued_segments,
            it->second.dropped_segments};
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
                if (!transcriptWithinBudget(transcript,
                        options_.max_result_text_bytes, options_.max_result_items)) {
                    error_ = "ASR transcript exceeds configured result budget";
                    ++dropped_;
                } else {
                    const bool repetition = hasRepetition(transcript.text);
                    results_.push_back({job.track_id, job.generation, std::move(transcript),
                        job.end_reason, repetition,
                        job.segment.audio.captureTime(),
                        job.segment.audio.captureTime().advancedBy(job.segment.audio.duration()),
                        job.start_sample,
                        job.start_sample + job.segment.audio.samples().size()});
                    // Prevent unbounded accumulation when application does not poll.
                    if (results_.size() > options_.max_pending_segments) {
                        results_.erase(results_.begin());
                        ++dropped_;
                        ++evicted_results_;
                    }
                    ++completed_;
                }
            }
            completeJobLocked(job.generation);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> guard{mutex_};
            error_ = e.what();
            completeJobLocked(job.generation);
            ++dropped_;
        } catch (...) {
            std::lock_guard<std::mutex> guard{mutex_};
            error_ = "Unknown ASR exception";
            completeJobLocked(job.generation);
            ++dropped_;
        }
    }
}

} // namespace audition
