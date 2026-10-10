#include "live_audio_tagging_worker.hpp"

#if LIBAUDITION_DEMO_HAS_SHERPA
#include <audition/backends/sherpa/audio_tagger.hpp>
#endif
#if LIBAUDITION_DEMO_HAS_YAMNET
#include <audition/backends/yamnet/audio_tagger.hpp>
#endif
#include <audition/audio/audio_buffer.hpp>
#include <audition/interfaces/classify.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace demo {

LiveAudioTaggingWorker::LiveAudioTaggingWorker(LiveTaggingOptions options)
    : window_samples_{static_cast<std::size_t>(options.sample_rate_hz) * (options.yamnet_model ? 1U : 2U)},
      stale_hops_{(static_cast<std::uint64_t>(options.sample_rate_hz) * 5U) /
                  std::max<std::uint32_t>(options.hop_size, 1U)} {
    if (options.sample_rate_hz != 16000U || options.hop_size == 0U ||
        options.model_path.empty() || options.labels_path.empty()) {
        throw std::runtime_error{
            "Live Sherpa audio tagging requires 16 kHz, a model and labels"};
    }
    worker_ = std::thread{[this, options = std::move(options)]() mutable {
        run(std::move(options));
    }};
}

LiveAudioTaggingWorker::~LiveAudioTaggingWorker() {
    {
        std::lock_guard<std::mutex> lock{mutex_};
        stopping_ = true;
        pending_.reset();
    }
    cv_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LiveAudioTaggingWorker::push(
    std::uint64_t track_id, const std::vector<float>& mono,
    std::uint64_t processed_hops) {
    if (track_id == 0U || mono.empty()) {
        return;
    }
    // Remove stale tracker IDs before the map can grow indefinitely.
    for (auto it = buffers_.begin(); it != buffers_.end();) {
        if (processed_hops > it->second.last_hop &&
            processed_hops - it->second.last_hop > stale_hops_) {
            it = buffers_.erase(it);
        } else {
            ++it;
        }
    }
    if (buffers_.size() >= 16U && buffers_.find(track_id) == buffers_.end()) {
        buffers_.erase(buffers_.begin());
    }
    auto& track = buffers_[track_id];
    track.last_hop = processed_hops;
    // Audio is already source-separated by ODAS. Never classify the raw
    // six-channel USB mix and attribute the result to a particular ID.
    const auto remaining = window_samples_ - track.samples.size();
    const auto appendCount = std::min(remaining, mono.size());
    track.samples.insert(track.samples.end(), mono.begin(),
                         mono.begin() + static_cast<std::ptrdiff_t>(appendCount));
    if (track.samples.size() != window_samples_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!pending_.has_value() && error_.empty() && !stopping_) {
            pending_ = Job{track_id, processed_hops, std::move(track.samples)};
            track.samples.clear();
            cv_.notify_one();
        } else {
            // Avoid a permanently full source window when the worker queue is busy.
            track.samples.clear();
        }
    }
}

std::optional<LiveTrackClassification> LiveAudioTaggingWorker::result(
    std::uint64_t track_id, std::uint64_t processed_hops) const {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = results_.find(track_id);
    if (it == results_.end() || processed_hops < it->second.hop ||
        processed_hops - it->second.hop > stale_hops_) {
        return std::nullopt;
    }
    return it->second.classification;
}

std::string LiveAudioTaggingWorker::error() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return error_;
}

void LiveAudioTaggingWorker::run(LiveTaggingOptions options) noexcept {
#if LIBAUDITION_DEMO_HAS_SHERPA || LIBAUDITION_DEMO_HAS_YAMNET
    try {
        std::unique_ptr<audition::IAudioClassifier> classifier;
        if (options.yamnet_model) {
#if LIBAUDITION_DEMO_HAS_YAMNET
            audition::YamnetOnnxOptions cfg{};
            cfg.model = options.model_path;
            cfg.labels = options.labels_path;
            cfg.top_k = 3U;
            classifier = std::make_unique<audition::YamnetAudioTagger>(std::move(cfg));
#else
            throw std::runtime_error{"YAMNet backend is not built"};
#endif
        } else {
#if LIBAUDITION_DEMO_HAS_SHERPA
        audition::SherpaAudioTaggingOptions cfg{};
        cfg.model = options.ced_model
            ? audition::SherpaAudioTaggingModel{
                audition::SherpaAudioTaggingCedModel{options.model_path}}
            : audition::SherpaAudioTaggingModel{
                audition::SherpaAudioTaggingZipformerModel{options.model_path}};
        cfg.labels = options.labels_path;
        cfg.top_k = 3;
        classifier = std::make_unique<audition::SherpaAudioTagger>(std::move(cfg));
#else
        throw std::runtime_error{"Sherpa backend is not built"};
#endif
        }
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock{mutex_};
                cv_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (stopping_) {
                    break;
                }
                job = std::move(*pending_);
                pending_.reset();
            }
            audition::AudioBuffer audio{
                std::move(job.samples),
                {options.sample_rate_hz, 1U, audition::AudioLayout::Interleaved},
                audition::Timestamp{}, job.hop};
            const auto classified = classifier->classify(audio.view());
            if (classified.classes.empty()) {
                continue;
            }
            const auto& top = classified.classes.front();
            std::lock_guard<std::mutex> lock{mutex_};
            if (stopping_) {
                break;
            }
            // Only the newest result for this source ID is authoritative.
            const auto old = results_.find(job.track_id);
            if (old == results_.end() || job.hop >= old->second.hop) {
                results_[job.track_id] = {
                    {top.label, top.probability.value()}, job.hop};
            }
            // Stale label caching must not grow with continuously recycled
            // ODAS tracker IDs.
            for (auto it = results_.begin(); it != results_.end();) {
                if (job.hop > it->second.hop &&
                    job.hop - it->second.hop > stale_hops_) {
                    it = results_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> lock{mutex_};
        error_ = exception.what();
    }
#else
    static_cast<void>(options);
    std::lock_guard<std::mutex> lock{mutex_};
    error_ = "Rebuild Qt demo with LIBAUDITION_WITH_SHERPA or LIBAUDITION_WITH_YAMNET";
#endif
}

}  // namespace demo
