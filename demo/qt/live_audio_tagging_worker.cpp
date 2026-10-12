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

#include <algorithm>
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
        bool yamnet_refinement_supported = false;
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
            auto yamnet = std::make_unique<audition::YamnetAudioTagger>(std::move(cfg));
            const std::size_t refinement_size = options.refined_window_samples != 0U
                ? options.refined_window_samples
                : static_cast<std::size_t>(options.sample_rate_hz) * 5U;
            const std::size_t quick_size = options.fast_window_samples != 0U
                ? options.fast_window_samples
                : yamnet->capabilities().audio.preferred_frame_count.value_or(15600U);
            if (!yamnet->supportsWindowSamples(quick_size)) {
                throw std::invalid_argument{"YAMNet fast window is incompatible with ONNX input shape"};
            }
            if (options.refined_window_samples != 0U &&
                !yamnet->supportsWindowSamples(refinement_size)) {
                throw std::invalid_argument{"YAMNet refined window is incompatible with ONNX input shape"};
            }
            yamnet_refinement_supported =
                refinement_size > quick_size && yamnet->supportsWindowSamples(refinement_size);
            classifier = std::move(yamnet);
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
        // The ONNX graph determines its actual required waveform length.
        // This matters for YAMNet exports with fixed 15,600-sample patches.
        const std::size_t window_samples =
            (options.efficientat_model || options.yamnet_model)
                ? classifier->capabilities().audio.preferred_frame_count.value_or(
                    static_cast<std::size_t>(options.sample_rate_hz))
                : static_cast<std::size_t>(options.sample_rate_hz) * 2U;
        const std::size_t fast_samples = options.fast_window_samples != 0U
            ? options.fast_window_samples : window_samples;
        // YAMNet: rapid ~0.975s patches at 50% overlap, then a 5-second
        // higher-context model independently updated every second.
        // Custom IAudioClassifier implementations and windows can instead
        // be injected into ProgressiveSourceClassificationRuntime directly.
        if (options.yamnet_model && yamnet_refinement_supported) {
#if LIBAUDITION_DEMO_HAS_YAMNET
            audition::YamnetOnnxOptions refined_options{};
            refined_options.model = options.model_path;
            refined_options.labels = options.labels_path;
            refined_options.top_k = 521U;
            const auto refined_samples = options.refined_window_samples != 0U
                ? options.refined_window_samples
                : static_cast<std::size_t>(options.sample_rate_hz) * 5U;
            const auto quick_hop = options.fast_hop_samples != 0U
                ? options.fast_hop_samples : std::max<std::size_t>(1U, fast_samples / 2U);
            const auto refined_hop = options.refined_hop_samples != 0U
                ? options.refined_hop_samples : static_cast<std::size_t>(options.sample_rate_hz);
            std::vector<audition::ProgressiveClassificationStage> stages;
            stages.push_back({"quick", std::move(classifier), fast_samples, quick_hop});
            stages.push_back({"refined",
                std::make_unique<audition::YamnetAudioTagger>(std::move(refined_options)),
                refined_samples, refined_hop});
            progressive_ = std::make_unique<audition::ProgressiveSourceClassificationRuntime>(
                options.sample_rate_hz, std::move(stages), 16U);
            return;
#endif
        }
        // Other models and fixed-window YAMNet remain available in single-
        // window mode. No changes to their classifier behavior or ABI.
        const std::size_t hop_samples = options.fast_hop_samples != 0U
            ? options.fast_hop_samples
            : options.yamnet_model
                ? std::max<std::size_t>(1U, fast_samples / 2U)
                : options.efficientat_model && fast_samples > options.sample_rate_hz
                    ? static_cast<std::size_t>(options.sample_rate_hz) : fast_samples;
        runtime_ = std::make_unique<audition::SourceClassificationRuntime>(
            std::move(classifier), options.sample_rate_hz, fast_samples,
            16U, hop_samples);
    } catch (const std::exception& exception) {
        initialization_error_ = exception.what();
    }
}

LiveAudioTaggingWorker::~LiveAudioTaggingWorker() = default;

void LiveAudioTaggingWorker::push(
    std::uint64_t track_id, const std::vector<float>& mono,
    std::uint64_t /*processed_hops*/) {
    if (mono.empty()) return;
    if (progressive_) progressive_->push(track_id, mono.data(), mono.size());
    else if (runtime_) runtime_->push(track_id, mono.data(), mono.size());
}

void LiveAudioTaggingWorker::forget(std::uint64_t track_id) {
    if (progressive_) progressive_->forget(track_id);
    else if (runtime_) runtime_->forget(track_id);
}

std::optional<LiveTrackClassification> LiveAudioTaggingWorker::result(
    std::uint64_t track_id, std::uint64_t /*processed_hops*/) const {
    std::optional<audition::ClassificationResult> prediction;
    std::string stage;
    std::optional<std::uint64_t> windowEnd;
    std::uint64_t windowSize = 0;
    if (progressive_) {
        const auto state = progressive_->status(track_id);
        prediction = state.result;
        if (state.selected_stage.has_value()) {
            stage = state.stages[*state.selected_stage].name;
            const auto& chosen = state.stages[*state.selected_stage].status;
            windowEnd = chosen.result_end_sample;
            windowSize = static_cast<std::uint64_t>(chosen.required_samples);
        }
    } else if (runtime_) {
        const auto status = runtime_->status(track_id);
        prediction = status.result;
        windowEnd = status.result_end_sample;
        windowSize = static_cast<std::uint64_t>(status.required_samples);
    }
    if (!prediction.has_value() || prediction->classes.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> supported;
    supported.reserve(prediction->classes.size());
    for (const auto& cls : prediction->classes) {
        supported.push_back(cls.label);
    }
    // Both backends are configured to return the full closed vocabulary.
    auto filtered = audition::selectVocabulary(*prediction, selection_, supported);
    LiveTrackClassification response{};
    response.stage = stage;
    if (windowEnd && *windowEnd >= windowSize) {
        response.window_start_sample = *windowEnd - windowSize;
        response.window_end_sample = *windowEnd;
        response.has_window = true;
    }
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
    if (progressive_) return "";
    if (!runtime_) return "Classification runtime unavailable";
    return runtime_->status(0U).error;
}

std::string LiveAudioTaggingWorker::status(std::uint64_t track_id) const {
    if (progressive_) {
        const auto state = progressive_->status(track_id);
        if (state.selected_stage.has_value()) {
            return state.stages[*state.selected_stage].name + " classification";
        }
        for (const auto& stage : state.stages) {
            if (stage.status.state == audition::SourceClassificationState::Error) {
                return stage.name + " error: " + stage.status.error;
            }
        }
        const auto& quick = state.stages.front().status;
        return "Quick collecting " + std::to_string(quick.collected_samples) +
               "/" + std::to_string(quick.required_samples);
    }
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
