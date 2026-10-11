#include "live_audio_tagging_worker.hpp"

#if LIBAUDITION_DEMO_HAS_SHERPA
#include <audition/backends/sherpa/audio_tagger.hpp>
#endif
#if LIBAUDITION_DEMO_HAS_YAMNET
#include <audition/backends/yamnet/audio_tagger.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_EFFICIENTAT
#include <audition/backends/efficientat/audio_tagger.hpp>
#endif

#include <stdexcept>
#include <sstream>
#include <utility>

namespace demo {

LiveAudioTaggingWorker::LiveAudioTaggingWorker(LiveTaggingOptions options)
    : selection_(options.vocabulary) {
    try {
        if (options.sample_rate_hz != 16000U || options.hop_size == 0U ||
            options.model_path.empty() || options.labels_path.empty()) {
            throw std::invalid_argument{"Audio tagging requires mono 16 kHz and model/labels"};
        }
        std::unique_ptr<audition::IAudioClassifier> classifier;
        if (options.efficientat_model) {
#if LIBAUDITION_DEMO_HAS_EFFICIENTAT
            audition::EfficientAtOnnxOptions cfg{};
            cfg.model = options.model_path;
            cfg.labels = options.labels_path;
            cfg.top_k = 527U;
            classifier = std::make_unique<audition::EfficientAt16kAudioTagger>(std::move(cfg));
#else
            throw std::runtime_error{"EfficientAT backend is not built"};
#endif
        } else if (options.yamnet_model) {
#if LIBAUDITION_DEMO_HAS_YAMNET
            audition::YamnetOnnxOptions cfg{};
            cfg.model = options.model_path;
            cfg.labels = options.labels_path;
            cfg.top_k = 521U;
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
            cfg.top_k = 527;
            classifier = std::make_unique<audition::SherpaAudioTagger>(std::move(cfg));
#else
            throw std::runtime_error{"Sherpa backend is not built"};
#endif
        }
        const std::size_t window_samples = options.efficientat_model
            ? classifier->capabilities().audio.preferred_frame_count
            : static_cast<std::size_t>(options.sample_rate_hz) *
                (options.yamnet_model ? 1U : 2U);
        runtime_ = std::make_unique<audition::SourceClassificationRuntime>(
            std::move(classifier), options.sample_rate_hz, window_samples);
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
    std::vector<std::string> supported;
    supported.reserve(current.result->classes.size());
    for (const auto& cls : current.result->classes) {
        supported.push_back(cls.label);
    }
    // Both backends are configured to return the full closed vocabulary.
    auto filtered = audition::selectVocabulary(*current.result, selection_, supported);
    LiveTrackClassification response{};
    response.unsupported_labels = std::move(filtered.unsupported_labels);
    std::ostringstream summary;
    for (std::size_t i = 0; i < filtered.classes.size(); ++i) {
        const auto& item = filtered.classes[i];
        if (i != 0U) summary << ", ";
        summary << item.label << " (" << item.probability.value() << ")";
        if (i == 0U) {
            response.label = item.label;
            response.probability = item.probability.value();
        }
    }
    response.top_classes = summary.str();
    return response;
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
