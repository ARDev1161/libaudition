#include "detail/sherpa_speaker_config.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

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

void validateRuntime(const SherpaRuntimeOptions& runtime) {
    require(runtime.num_threads > 0, "Sherpa num_threads must be positive");
    require(runtime.execution.device_index < 0,
            "Sherpa adapter does not yet expose execution device_index");
    require(runtime.execution.precision == PrecisionPreference::Auto,
            "Sherpa adapter does not yet expose precision selection");
    require(runtime.execution.provider_options.empty(),
            "Sherpa adapter does not yet expose provider_options");
    require(runtime.execution.allow_fallback,
            "Sherpa adapter cannot currently enforce allow_fallback=false");
    if (runtime.execution.provider.empty()) {
        require(runtime.execution.device_class == DeviceClass::Auto ||
                    runtime.execution.device_class == DeviceClass::Cpu,
                "Non-CPU Sherpa execution targets require an explicit provider name");
    }
}

std::filesystem::path resolvePath(
    const std::filesystem::path& path,
    const std::optional<ModelDescriptor>& descriptor) {
    if (path.empty() || path.is_absolute() || !descriptor.has_value() ||
        descriptor->artifact_path.empty()) {
        return path;
    }
    return (descriptor->artifact_path / path).lexically_normal();
}

std::string resolved(
    const std::filesystem::path& path,
    const std::optional<ModelDescriptor>& descriptor) {
    return resolvePath(path, descriptor).string();
}

void requirePath(const std::filesystem::path& path, const char* message) {
    require(!path.empty(), message);
}

void requireFiniteNonNegative(float value, const char* message) {
    require(std::isfinite(value) && value >= 0.0F, message);
}

}  // namespace

void validateSherpaSpeakerEmbeddingOptions(
    const SherpaSpeakerEmbeddingOptions& options) {
    validateDescriptor(options.model_descriptor);
    validateRuntime(options.runtime);
    requirePath(options.model, "Sherpa speaker embedding model path is required");
    require(options.sample_rate_hz > 0U,
            "Sherpa speaker embedding sample rate must be non-zero");
    require(options.sample_rate_hz <=
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "Sherpa speaker embedding sample rate exceeds backend range");
}

void validateSherpaSpeakerIdentifierOptions(
    const SherpaSpeakerIdentifierOptions& options) {
    require(options.embedding_dimension > 0U,
            "Sherpa speaker identifier embedding dimension must be non-zero");
    require(options.embedding_dimension <=
                static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
            "Sherpa speaker identifier embedding dimension exceeds backend range");
}

void validateSherpaSpeakerDiarizationOptions(
    const SherpaSpeakerDiarizationOptions& options) {
    validateDescriptor(options.segmentation_model_descriptor);
    validateDescriptor(options.embedding_model_descriptor);
    validateRuntime(options.segmentation_runtime);
    validateRuntime(options.embedding_runtime);

    requirePath(options.segmentation_model,
                "Sherpa diarization segmentation model path is required");
    requirePath(options.embedding_model,
                "Sherpa diarization embedding model path is required");
    require(std::isfinite(options.segmentation_window_shift_ratio) &&
                options.segmentation_window_shift_ratio > 0.0F &&
                options.segmentation_window_shift_ratio <= 1.0F,
            "Sherpa diarization window shift ratio must be in (0, 1]");
    require(options.num_clusters >= 0,
            "Sherpa diarization num_clusters must be non-negative");
    require(std::isfinite(options.clustering_threshold) &&
                options.clustering_threshold > 0.0F,
            "Sherpa diarization clustering threshold must be positive");
    requireFiniteNonNegative(options.min_duration_on,
                             "Sherpa diarization min_duration_on must be non-negative");
    requireFiniteNonNegative(options.min_duration_off,
                             "Sherpa diarization min_duration_off must be non-negative");
}

namespace sherpa_detail {

sherpa_onnx::cxx::SpeakerEmbeddingExtractorConfig
makeSpeakerEmbeddingConfig(const SherpaSpeakerEmbeddingOptions& options) {
    validateSherpaSpeakerEmbeddingOptions(options);

    sherpa_onnx::cxx::SpeakerEmbeddingExtractorConfig config{};
    config.model = resolved(options.model, options.model_descriptor);
    config.num_threads = options.runtime.num_threads;
    config.debug = options.runtime.debug;
    config.provider = providerFor(options.runtime);
    return config;
}

sherpa_onnx::cxx::OfflineSpeakerDiarizationConfig
makeSpeakerDiarizationConfig(const SherpaSpeakerDiarizationOptions& options) {
    validateSherpaSpeakerDiarizationOptions(options);

    sherpa_onnx::cxx::OfflineSpeakerDiarizationConfig config{};
    config.segmentation.pyannote.model =
        resolved(options.segmentation_model, options.segmentation_model_descriptor);
    config.segmentation.pyannote.window_shift_ratio =
        options.segmentation_window_shift_ratio;
    config.segmentation.num_threads = options.segmentation_runtime.num_threads;
    config.segmentation.debug = options.segmentation_runtime.debug;
    config.segmentation.provider = providerFor(options.segmentation_runtime);

    config.embedding.model =
        resolved(options.embedding_model, options.embedding_model_descriptor);
    config.embedding.num_threads = options.embedding_runtime.num_threads;
    config.embedding.debug = options.embedding_runtime.debug;
    config.embedding.provider = providerFor(options.embedding_runtime);

    config.clustering.num_clusters = options.num_clusters;
    config.clustering.threshold = options.clustering_threshold;
    config.clustering.compute_confidence = options.compute_confidence;
    config.min_duration_on = options.min_duration_on;
    config.min_duration_off = options.min_duration_off;
    return config;
}

std::string speakerModelId(const SherpaSpeakerEmbeddingOptions& options) {
    if (!options.model_id.empty()) {
        return options.model_id;
    }
    if (options.model_descriptor.has_value() &&
        !options.model_descriptor->id.empty()) {
        return options.model_descriptor->id;
    }
    return options.model.filename().string();
}

}  // namespace sherpa_detail
}  // namespace audition
