#include <audition/classify/source_classification_runtime.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <audition/audio/audio_buffer.hpp>

namespace audition {

SourceClassificationRuntime::SourceClassificationRuntime(
    std::unique_ptr<IAudioClassifier> classifier,
    std::uint32_t sample_rate_hz, std::size_t window_samples,
    std::size_t max_sources)
    : sample_rate_hz_(sample_rate_hz),
      window_samples_(window_samples),
      max_sources_(max_sources),
      classifier_(std::move(classifier)) {
    if (!classifier_ || sample_rate_hz_ == 0U ||
        window_samples_ == 0U || max_sources_ == 0U) {
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
    if (stopping_ || !error_.empty()) return;
    auto it = buffers_.find(source_id);
    if (it == buffers_.end()) {
        if (buffers_.size() >= max_sources_) {
            // Do not silently misattribute old source ID results.
            buffers_.erase(buffers_.begin());
        }
        Buffer fresh{};
        fresh.generation = next_generation_++;
        it = buffers_.emplace(source_id, std::move(fresh)).first;
    }
    auto& buffer = it->second.samples;
    for (std::size_t i = 0U; i < count; ++i) {
        if (!std::isfinite(samples[i])) {
            error_ = "Nonfinite source audio sample";
            return;
        }
        buffer.push_back(samples[i]);
        if (buffer.size() == window_samples_) {
            if (pending_.size() < max_sources_ && std::none_of(pending_.begin(), pending_.end(), [source_id](const Job& j){ return j.source_id == source_id; })) {
                pending_.push_back(Job{source_id, it->second.generation, std::move(buffer)});
                buffer.clear();
                cv_.notify_one();
            } else {
                buffer.clear();
                ++dropped_windows_;
            }
        }
    }
}

SourceClassificationStatus SourceClassificationRuntime::status(
    std::uint64_t source_id) const {
    std::lock_guard<std::mutex> lock{mutex_};
    SourceClassificationStatus result{};
    result.required_samples = window_samples_;
    result.dropped_windows = dropped_windows_;
    if (!error_.empty()) {
        result.state = SourceClassificationState::Error;
        result.error = error_;
        return result;
    }
    const auto it = buffers_.find(source_id);
    if (it == buffers_.end()) return result;
    result.collected_samples = it->second.samples.size();
    if (it->second.result.has_value() &&
        std::chrono::steady_clock::now() - it->second.last_result_at < kResultTtl) {
        result.result = it->second.result;
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

void SourceClassificationRuntime::forget(std::uint64_t source_id) {
    std::lock_guard<std::mutex> lock{mutex_};
    buffers_.erase(source_id);
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
        try {
            AudioBuffer audio{std::move(job.samples),
                              {sample_rate_hz_, 1U, AudioLayout::Interleaved},
                              Timestamp{}, 0U};
            auto result = classifier_->classify(audio.view());
            std::lock_guard<std::mutex> lock{mutex_};
            if (const auto it = buffers_.find(job.source_id);
                it != buffers_.end() && it->second.generation == job.generation) {
                it->second.result = std::move(result);
                it->second.last_result_at = std::chrono::steady_clock::now();
            }
            active_source_.reset();
        } catch (const std::exception& exception) {
            std::lock_guard<std::mutex> lock{mutex_};
            error_ = exception.what();
            active_source_.reset();
            return;
        } catch (...) {
            std::lock_guard<std::mutex> lock{mutex_};
            error_ = "Unknown classification exception";
            active_source_.reset();
            return;
        }
    }
}
} // namespace audition
