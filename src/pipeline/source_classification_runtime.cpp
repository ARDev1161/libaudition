#include <audition/classify/source_classification_runtime.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <audition/audio/audio_buffer.hpp>

namespace audition {
namespace {

// Drop only the retired source epoch. Unrelated sources may already have
// completed events waiting for an asynchronous consumer.
void purgeCompletedEpoch(
    std::deque<CompletedSourceClassification>& completed,
    std::uint64_t source_id, std::uint64_t generation) {
    completed.erase(std::remove_if(completed.begin(), completed.end(),
        [source_id, generation](const CompletedSourceClassification& event) {
            return event.source_id == source_id &&
                   event.generation == generation;
        }), completed.end());
}

}  // namespace

SourceClassificationRuntime::SourceClassificationRuntime(
    std::unique_ptr<IAudioClassifier> classifier,
    std::uint32_t sample_rate_hz, std::size_t window_samples,
    std::size_t max_sources, std::size_t hop_samples)
    : sample_rate_hz_(sample_rate_hz),
      window_samples_(window_samples),
      max_sources_(max_sources),
      hop_samples_(hop_samples == 0U ? window_samples : hop_samples),
      classifier_(std::move(classifier)) {
    if (!classifier_ || sample_rate_hz_ == 0U ||
        window_samples_ == 0U || max_sources_ == 0U ||
        hop_samples_ == 0U || hop_samples_ > window_samples_) {
        throw std::invalid_argument{"Invalid source classification runtime configuration"};
    }
    worker_ = std::thread{[this] { run(); }};
}

SourceClassificationRuntime::~SourceClassificationRuntime() {
    {
        std::lock_guard<std::mutex> lock{mutex_};
        stopping_ = true;
        pending_.clear();
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void SourceClassificationRuntime::push(
    std::uint64_t source_id, const float* samples, std::size_t count) {
    if (samples == nullptr || count == 0U) return;
    std::lock_guard<std::mutex> lock{mutex_};
    if (stopping_) return;
    auto it = buffers_.find(source_id);
    if (it == buffers_.end()) {
        if (buffers_.size() >= max_sources_) {
            // Evict the least-recently received source, not the lowest ID.
            // Remove any pending job belonging to the retired generation.
            const auto victim = std::min_element(buffers_.begin(), buffers_.end(),
                [](const auto& a, const auto& b) {
                    return a.second.last_seen_order < b.second.last_seen_order;
                });
            const auto evicted_id = victim->first;
            const auto evicted_generation = victim->second.generation;
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                [evicted_id, evicted_generation](const Job& job) {
                    return job.source_id == evicted_id &&
                           job.generation == evicted_generation;
                }), pending_.end());
            // Eviction ends the source epoch, just like forget(). A delayed
            // GUI/ROS consumer must not receive completed predictions from
            // an epoch that the runtime has already retired.
            purgeCompletedEpoch(completed_, evicted_id, evicted_generation);
            buffers_.erase(victim);
        }
        Buffer fresh{};
        fresh.generation = next_generation_++;
        it = buffers_.emplace(source_id, std::move(fresh)).first;
    }
    it->second.last_seen_order = next_seen_order_++;
    if (!it->second.error.empty()) return;
    auto& buffer = it->second.samples;
    for (std::size_t i = 0U; i < count; ++i) {
        if (!std::isfinite(samples[i])) {
            it->second.error = "Nonfinite source audio sample";
            it->second.samples.clear();
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                [source_id, generation=it->second.generation](const Job& job) {
                    return job.source_id == source_id && job.generation == generation;
                }), pending_.end());
            purgeCompletedEpoch(completed_, source_id, it->second.generation);
            it->second.result.reset();
            it->second.result_end_sample.reset();
            return;
        }
        buffer.push_back(samples[i]);
        ++it->second.received_samples;
        if (buffer.size() == window_samples_) {
            // Prefer the newest queued window for a track when inference
            // cannot keep up. An older pending window is replaceable, but
            // an in-progress window is immutable. At most one job per source
            // remains queued and other sources retain their queue positions.
            const auto waiting = std::find_if(pending_.begin(), pending_.end(),
                [source_id, generation=it->second.generation](const Job& job) {
                    return job.source_id == source_id &&
                           job.generation == generation;
                });
            if (waiting != pending_.end()) {
                waiting->samples = buffer;
                waiting->end_sample = it->second.received_samples;
                waiting->enqueued_at = std::chrono::steady_clock::now();
                ++it->second.dropped_windows;
            } else if (pending_.size() < max_sources_) {
                pending_.push_back(Job{source_id, it->second.generation,
                                       it->second.received_samples,
                                       std::chrono::steady_clock::now(), buffer});
                cv_.notify_one();
            } else {
                ++it->second.dropped_windows;
            }
            buffer.erase(buffer.begin(),
                         buffer.begin() + static_cast<std::ptrdiff_t>(hop_samples_));
        }
    }
}

SourceClassificationStatus SourceClassificationRuntime::status(
    std::uint64_t source_id) const {
    std::lock_guard<std::mutex> lock{mutex_};
    SourceClassificationStatus result{};
    result.required_samples = window_samples_;
    const auto it = buffers_.find(source_id);
    if (it == buffers_.end()) return result;
    result.dropped_windows = it->second.dropped_windows;
    result.completed_windows = it->second.completed_windows;
    result.last_inference_ms = it->second.last_inference_ms;
    result.last_queue_wait_ms = it->second.last_queue_wait_ms;
    if (it->second.completed_windows != 0U) {
        const double completed = static_cast<double>(it->second.completed_windows);
        result.mean_inference_ms = it->second.total_inference_ms / completed;
        result.mean_queue_wait_ms = it->second.total_queue_wait_ms / completed;
        result.max_inference_ms = it->second.max_inference_ms;
        result.max_queue_wait_ms = it->second.max_queue_wait_ms;
    }
    result.received_samples = it->second.received_samples;
    if (!it->second.error.empty()) {
        result.state = SourceClassificationState::Error;
        result.error = it->second.error;
        return result;
    }
    result.collected_samples = it->second.samples.size();
    result.received_samples = it->second.received_samples;
    if (it->second.result.has_value() &&
        std::chrono::steady_clock::now() - it->second.last_result_at < kResultTtl) {
        result.result = it->second.result;
        result.result_end_sample = it->second.result_end_sample;
    }
    if (active_source_ == source_id && active_generation_ == it->second.generation) {
        result.state = SourceClassificationState::Inferencing;
    } else if (std::any_of(pending_.begin(), pending_.end(), [source_id, gen=it->second.generation](const Job& j){ return j.source_id == source_id && j.generation == gen; })) {
        result.state = SourceClassificationState::Queued;
    } else if (result.result.has_value()) {
        result.state = SourceClassificationState::Classified;
    } else {
        result.state = SourceClassificationState::Collecting;
    }
    return result;
}

std::vector<CompletedSourceClassification> SourceClassificationRuntime::takeCompleted() {
    std::lock_guard<std::mutex> lock{mutex_};
    std::vector<CompletedSourceClassification> out;
    out.reserve(completed_.size());
    while (!completed_.empty()) {
        out.push_back(std::move(completed_.front()));
        completed_.pop_front();
    }
    return out;
}

void SourceClassificationRuntime::forget(std::uint64_t source_id) {
    std::lock_guard<std::mutex> lock{mutex_};
    buffers_.erase(source_id);
    completed_.erase(std::remove_if(completed_.begin(), completed_.end(),
        [source_id](const CompletedSourceClassification& event) {
            return event.source_id == source_id;
        }), completed_.end());
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [source_id](const Job& j){return j.source_id == source_id;}), pending_.end());
}

void SourceClassificationRuntime::run() noexcept {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock{mutex_};
            cv_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) return;
            job = std::move(pending_.front());
            pending_.pop_front();
            active_source_ = job.source_id;
            active_generation_ = job.generation;
        }
        const auto inference_started = std::chrono::steady_clock::now();
        const double queue_wait_ms = std::chrono::duration<double, std::milli>(
            inference_started - job.enqueued_at).count();
        try {
            AudioBuffer audio{std::move(job.samples),
                              {sample_rate_hz_, 1U, AudioLayout::Interleaved},
                              Timestamp{}, 0U};
            auto result = classifier_->classify(audio.view());
            const auto inference_finished = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock{mutex_};
            if (const auto it = buffers_.find(job.source_id);
                it != buffers_.end() && it->second.generation == job.generation &&
                it->second.error.empty()) {
                it->second.result = std::move(result);
                it->second.result_end_sample = job.end_sample;
                // Deliver every completed window, not just a GUI-polled
                // latest snapshot. Bound retained event copies for slow clients.
                if (completed_.size() >= max_sources_ * 4U)
                    completed_.pop_front();
                completed_.push_back({job.source_id, job.generation,
                    job.end_sample - static_cast<std::uint64_t>(window_samples_),
                    job.end_sample, *it->second.result});
                it->second.last_result_at = inference_finished;
                ++it->second.completed_windows;
                it->second.last_inference_ms = std::chrono::duration<double, std::milli>(
                    inference_finished - inference_started).count();
                it->second.last_queue_wait_ms = queue_wait_ms;
                it->second.total_inference_ms += *it->second.last_inference_ms;
                it->second.total_queue_wait_ms += queue_wait_ms;
                it->second.max_inference_ms = std::max(
                    it->second.max_inference_ms, *it->second.last_inference_ms);
                it->second.max_queue_wait_ms = std::max(
                    it->second.max_queue_wait_ms, queue_wait_ms);
            }
            active_source_.reset();
        } catch (const std::exception& exception) {
            std::lock_guard<std::mutex> lock{mutex_};
            if (const auto it = buffers_.find(job.source_id);
                it != buffers_.end() && it->second.generation == job.generation) {
                it->second.error = exception.what();
                it->second.samples.clear();
                it->second.result.reset();
                it->second.result_end_sample.reset();
                pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                    [source=job.source_id, generation=job.generation](const Job& queued) {
                        return queued.source_id == source && queued.generation == generation;
                    }), pending_.end());
                purgeCompletedEpoch(completed_, job.source_id, job.generation);
            }
            active_source_.reset();
        } catch (...) {
            std::lock_guard<std::mutex> lock{mutex_};
            if (const auto it = buffers_.find(job.source_id);
                it != buffers_.end() && it->second.generation == job.generation) {
                it->second.error = "Unknown classification exception";
                it->second.samples.clear();
                it->second.result.reset();
                it->second.result_end_sample.reset();
                pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                    [source=job.source_id, generation=job.generation](const Job& queued) {
                        return queued.source_id == source && queued.generation == generation;
                    }), pending_.end());
                purgeCompletedEpoch(completed_, job.source_id, job.generation);
            }
            active_source_.reset();
        }
    }
}
} // namespace audition
