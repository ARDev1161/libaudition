#include "detail/sherpa_speech_denoiser.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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

std::filesystem::path resolvePath(
    const std::filesystem::path& path,
    const std::optional<ModelDescriptor>& descriptor) {
    if (path.empty() || path.is_absolute() || !descriptor.has_value() ||
        descriptor->artifact_path.empty()) {
        return path;
    }
    return (descriptor->artifact_path / path).lexically_normal();
}

void validateCommon(const SherpaSpeechDenoiserOptions& options) {
    validateDescriptor(options.model_descriptor);
    static_cast<void>(sherpa_detail::providerFor(options.runtime));

    std::visit(
        [](const auto& model) {
            require(!model.model.empty(),
                    "Sherpa speech-denoiser model path is required");
            using T = std::decay_t<decltype(model)>;
            if constexpr (std::is_same_v<T, SherpaSpeechDenoiserDpdfNetModel>) {
                require(std::isfinite(model.attenuation_limit_db) &&
                            model.attenuation_limit_db >= 0.0F,
                        "Sherpa DPDFNet attenuation limit must be finite and non-negative");
            }
        },
        options.model);
}

sherpa_onnx::cxx::OfflineSpeechDenoiserModelConfig makeModelConfig(
    const SherpaSpeechDenoiserOptions& options) {
    sherpa_onnx::cxx::OfflineSpeechDenoiserModelConfig config{};
    config.num_threads = options.runtime.num_threads;
    config.debug = options.runtime.debug;
    config.provider = sherpa_detail::providerFor(options.runtime);

    std::visit(
        [&](const auto& model) {
            using T = std::decay_t<decltype(model)>;
            const auto resolved =
                resolvePath(model.model, options.model_descriptor).string();
            if constexpr (std::is_same_v<T, SherpaSpeechDenoiserGtcrnModel>) {
                config.gtcrn.model = resolved;
            } else {
                config.dpdfnet.model = resolved;
                config.dpdfnet.attenuation_limit_db =
                    model.attenuation_limit_db;
            }
        },
        options.model);

    return config;
}

}  // namespace

void validateSherpaOfflineSpeechDenoiserOptions(
    const SherpaSpeechDenoiserOptions& options) {
    validateCommon(options);
}

void validateSherpaStreamingSpeechDenoiserOptions(
    const SherpaSpeechDenoiserOptions& options) {
    validateCommon(options);
    const auto* dpdfnet =
        std::get_if<SherpaSpeechDenoiserDpdfNetModel>(&options.model);
    require(dpdfnet == nullptr || dpdfnet->attenuation_limit_db == 0.0F,
            "Sherpa streaming DPDFNet does not expose attenuation limiting");
}

namespace sherpa_detail {

sherpa_onnx::cxx::OfflineSpeechDenoiserConfig makeOfflineSpeechDenoiserConfig(
    const SherpaSpeechDenoiserOptions& options) {
    validateSherpaOfflineSpeechDenoiserOptions(options);
    sherpa_onnx::cxx::OfflineSpeechDenoiserConfig config{};
    config.model = makeModelConfig(options);
    return config;
}

sherpa_onnx::cxx::OnlineSpeechDenoiserConfig makeStreamingSpeechDenoiserConfig(
    const SherpaSpeechDenoiserOptions& options) {
    validateSherpaStreamingSpeechDenoiserOptions(options);
    sherpa_onnx::cxx::OnlineSpeechDenoiserConfig config{};
    config.model = makeModelConfig(options);
    return config;
}

void validateDenoiserFormat(const AudioFormat& format,
                            std::uint32_t expected_sample_rate_hz,
                            const char* role) {
    if (!format.valid()) {
        throw Error{ErrorCode::InvalidArgument,
                    std::string{"Sherpa "} + role +
                        " requires a valid audio format"};
    }
    if (format.channel_count != 1U ||
        format.sample_rate_hz != expected_sample_rate_hz) {
        throw Error{ErrorCode::UnsupportedFormat,
                    std::string{"Sherpa "} + role +
                        " requires mono audio at the model sample rate"};
    }
}

void validateDenoiserInput(AudioView audio,
                           const AudioFormat& session_format,
                           const char* role) {
    if (!audio.format().valid()) {
        throw Error{ErrorCode::InvalidArgument,
                    std::string{"Sherpa "} + role +
                        " requires valid input audio"};
    }
    if (audio.format().sample_rate_hz != session_format.sample_rate_hz ||
        audio.format().channel_count != session_format.channel_count ||
        audio.format().layout != session_format.layout) {
        throw Error{ErrorCode::UnsupportedFormat,
                    std::string{"Sherpa "} + role +
                        " input format does not match the session format"};
    }
    if (audio.sampleCount() == 0U) {
        throw Error{ErrorCode::InvalidArgument,
                    std::string{"Sherpa "} + role +
                        " requires non-empty input audio"};
    }
    if (audio.sampleCount() >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw Error{ErrorCode::InvalidArgument,
                    std::string{"Sherpa "} + role +
                        " input exceeds the native sample-count range"};
    }
}

AudioBuffer denoisedAudioBuffer(sherpa_onnx::cxx::DenoisedAudio output,
                                const AudioFormat& format,
                                Timestamp capture_time,
                                std::uint64_t sequence_number,
                                const char* role) {
    if (output.sample_rate <= 0 ||
        static_cast<std::uint32_t>(output.sample_rate) !=
            format.sample_rate_hz) {
        throw Error{ErrorCode::ProcessingError,
                    std::string{"Sherpa "} + role +
                        " returned an unexpected output sample rate"};
    }

    return AudioBuffer{std::move(output.samples), format, capture_time,
                       sequence_number};
}

}  // namespace sherpa_detail
}  // namespace audition
