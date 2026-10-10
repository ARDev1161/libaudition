#include "live_audio_tagging_worker.hpp"

#if LIBAUDITION_DEMO_HAS_SHERPA
#include <audition/backends/sherpa/audio_tagger.hpp>
#endif
#if LIBAUDITION_DEMO_HAS_YAMNET
#include <audition/backends/yamnet/audio_tagger.hpp>
#endif

#include <stdexcept>
#include <utility>

namespace demo {

LiveAudioTaggingWorker::LiveAudioTaggingWorker(LiveTaggingOptions options) {
    try {
        if (options.sample_rate_hz != 16000U || options.hop_size == 0U ||
            options.model_path.empty() || options.labels_path.empty()) {
            throw std::invalid_argument{"Audio tagging requires mono 16 kHz and model/labels"};
        }
        std::unique_ptr<audition::IAudioClassifier> classifier;
        if (options.yamnet_model) {
#if LIBAUDITION_DEMO_HAS_YAMNET
            audition::YamnetOnnxOptions cfg{};
            cfg.model = options.model_path;
            cfg.labels = options.labels_path;
            cfg.top_k = 5U;
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
            cfg.top_k = 5;
            classifier = std::make_unique<audition::SherpaAudioTagger>(std::move(cfg));
#else
            throw std::runtime_error{"Sherpa backend is not built"};
#endif
        }
        runtime_ = std::make_unique<audition::SourceClassificationRuntime>(
            std::move(classifier), options.sample_rate_hz,
            static_cast<std::size_t>(options.sample_rate_hz) *
                (options.yamnet_model ? 1U : 2U));
    } catch (const std::exception& exception) {
        initialization_error_ = exception.what();
    }
}

LiveAudioTaggingWorker::~LiveAudioTaggingWorker() = default;

void LiveAudioTaggingWorker::push(
    std::uint64_t track_id, const std::vector<float>& mono,
    std::uint64_t /*processed_hops*/) {
    if (runtime_ && !mono.empty()) {
        runtime_->push(track_id, mono.data(), mono.size());
    }
}

std::optional<LiveTrackClassification> LiveAudioTaggingWorker::result(
    std::uint64_t track_id, std::uint64_t /*processed_hops*/) const {
    if (!runtime_) return std::nullopt;
    const auto current = runtime_->status(track_id);
    if (!current.result.has_value() || current.result->classes.empty()) {
        return std::nullopt;
    }
    const auto& top = current.result->classes.front();
    return LiveTrackClassification{top.label, top.probability.value()};
}

std::string LiveAudioTaggingWorker::error() const {
    if (!initialization_error_.empty()) return initialization_error_;
    if (!runtime_) return "Classification runtime unavailable";
    return runtime_->status(0U).error;
}

std::string LiveAudioTaggingWorker::status(std::uint64_t track_id) const {
    if (!runtime_) return "Error: " + initialization_error_;
    const auto current = runtime_->status(track_id);
    using State = audition::SourceClassificationState;
    switch (current.state) {
    case State::WaitingForAudio: return "Waiting for SSS";
    case State::Collecting:
        return "Collecting " + std::to_string(current.collected_samples) +
               "/" + std::to_string(current.required_samples);
    case State::Queued: return "Queued";
    case State::Inferencing: return "Inferencing";
    case State::Classified: return "Classified";
    case State::Error: return "Error: " + current.error;
    }
    return "Unknown";
}

} // namespace demo
