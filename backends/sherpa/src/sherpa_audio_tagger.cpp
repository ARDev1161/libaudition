#include <audition/backends/sherpa/audio_tagger.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"

namespace audition {
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

void validateDescriptor(const std::optional<ModelDescriptor>& descriptor) {
    if (!descriptor.has_value() || descriptor->backend.empty()) {
        return;
    }
    require(descriptor->backend == "sherpa-onnx" ||
                descriptor->backend == "sherpa_onnx" ||
                descriptor->backend == "sherpa",
            "ModelDescriptor backend must identify sherpa-onnx");
}

std::string resolved(const std::filesystem::path& path,
                     const std::optional<ModelDescriptor>& descriptor) {
    if (path.empty() || path.is_absolute() || !descriptor.has_value() ||
        descriptor->artifact_path.empty()) {
        return path.string();
    }
    return (descriptor->artifact_path / path).lexically_normal().string();
}

sherpa_onnx::cxx::AudioTaggingConfig makeAudioTaggingConfig(
    const SherpaAudioTaggingOptions& options) {
    validateSherpaAudioTaggingOptions(options);

    sherpa_onnx::cxx::AudioTaggingConfig config{};
    config.model.num_threads = options.runtime.num_threads;
    config.model.debug = options.runtime.debug;
    config.model.provider = sherpa_detail::providerFor(options.runtime);
    config.labels = resolved(options.labels, options.model_descriptor);
    config.top_k = options.top_k;

    std::visit(
        [&](const auto& model) {
            using T = std::decay_t<decltype(model)>;
            if constexpr (std::is_same_v<T, SherpaAudioTaggingZipformerModel>) {
                config.model.zipformer.model =
                    resolved(model.model, options.model_descriptor);
            } else {
                config.model.ced = resolved(model.model, options.model_descriptor);
            }
        },
        options.model);

    return config;
}

}  // namespace

void validateSherpaAudioTaggingOptions(const SherpaAudioTaggingOptions& options) {
    validateDescriptor(options.model_descriptor);
    static_cast<void>(sherpa_detail::providerFor(options.runtime));

    require(!options.labels.empty(),
            "Sherpa audio-tagging labels path is required");
    require(options.sample_rate_hz > 0U,
            "Sherpa audio-tagging sample rate must be non-zero");
    require(options.sample_rate_hz <=
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "Sherpa audio-tagging sample rate exceeds backend range");
    require(options.top_k > 0,
            "Sherpa audio-tagging top_k must be positive");

    std::visit(
        [](const auto& model) {
            require(!model.model.empty(),
                    "Sherpa audio-tagging model path is required");
        },
        options.model);
}

class SherpaAudioTagger::Impl {
public:
    explicit Impl(SherpaAudioTaggingOptions options)
        : options_(std::move(options)),
          tagger_(sherpa_onnx::cxx::AudioTagging::Create(
              makeAudioTaggingConfig(options_))) {
        if (tagger_.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx audio tagger"};
        }
    }

    [[nodiscard]] ClassificationResult classify(AudioView audio) const {
        sherpa_detail::validateMonoAudio(
            audio, options_.sample_rate_hz, "audio tagging");
        if (audio.sampleCount() == 0U) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa audio tagging requires non-empty audio"};
        }
        if (audio.sampleCount() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa audio-tagging input exceeds backend range"};
        }

        std::lock_guard<std::mutex> lock{mutex_};
        auto stream = tagger_.CreateStream();
        if (stream.Get() == nullptr) {
            throw Error{ErrorCode::ProcessingError,
                        "Failed to create sherpa-onnx audio-tagging stream"};
        }

        stream.AcceptWaveform(
            static_cast<std::int32_t>(audio.format().sample_rate_hz),
            audio.data(),
            static_cast<std::int32_t>(audio.sampleCount()));

        const auto events = tagger_.Compute(&stream, options_.top_k);
        ClassificationResult result{};
        result.classes.reserve(events.size());

        for (const auto& event : events) {
            if (event.name.empty()) {
                throw Error{ErrorCode::ProcessingError,
                            "Sherpa audio tagging returned an empty label"};
            }
            if (!std::isfinite(event.prob) ||
                event.prob < 0.0F || event.prob > 1.0F) {
                throw Error{ErrorCode::ProcessingError,
                            "Sherpa audio tagging returned an invalid probability"};
            }
            result.classes.push_back(
                {event.name, Probability::from(static_cast<double>(event.prob))});
        }
        return result;
    }

    SherpaAudioTaggingOptions options_{};
    mutable sherpa_onnx::cxx::AudioTagging tagger_;
    mutable std::mutex mutex_{};
};

SherpaAudioTagger::SherpaAudioTagger(SherpaAudioTaggingOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaAudioTagger::~SherpaAudioTagger() = default;
SherpaAudioTagger::SherpaAudioTagger(SherpaAudioTagger&&) noexcept = default;
SherpaAudioTagger& SherpaAudioTagger::operator=(SherpaAudioTagger&&) noexcept = default;

BackendInfo SherpaAudioTagger::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

ClassifierCapabilities SherpaAudioTagger::capabilities() const {
    ClassifierCapabilities capabilities{};
    capabilities.open_vocabulary = false;
    capabilities.embeddings = false;
    capabilities.audio =
        sherpa_detail::monoRequirements(impl_->options_.sample_rate_hz);
    capabilities.execution.device_classes = {
        impl_->options_.runtime.execution.device_class};
    capabilities.execution.providers = {
        sherpa_detail::providerFor(impl_->options_.runtime)};
    return capabilities;
}

ClassificationResult SherpaAudioTagger::classify(AudioView audio) const {
    return impl_->classify(audio);
}

const SherpaAudioTaggingOptions& SherpaAudioTagger::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
