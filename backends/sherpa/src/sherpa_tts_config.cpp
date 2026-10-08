#include "detail/sherpa_tts.hpp"

#if defined(LIBAUDITION_SHERPA_TTS_ENABLED)

#include <algorithm>
#include <cmath>
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

void requirePath(const std::filesystem::path& path, const char* message) {
    require(!path.empty(), message);
}

void requirePositive(float value, const char* message) {
    require(std::isfinite(value) && value > 0.0F, message);
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

std::string resolved(const std::filesystem::path& path,
                     const std::optional<ModelDescriptor>& descriptor) {
    return resolvePath(path, descriptor).string();
}

void validateModel(const SherpaTtsModel& model) {
    std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SherpaTtsVitsModel>) {
                requirePath(value.model, "Sherpa VITS model path is required");
                requirePath(value.tokens, "Sherpa VITS tokens path is required");
                require(!( !value.lexicon.empty() && !value.data_dir.empty()),
                        "Sherpa VITS lexicon and data_dir are mutually exclusive in libaudition");
                requirePositive(value.noise_scale,
                                "Sherpa VITS noise_scale must be positive");
                requirePositive(value.noise_scale_w,
                                "Sherpa VITS noise_scale_w must be positive");
                requirePositive(value.length_scale,
                                "Sherpa VITS length_scale must be positive");
            } else if constexpr (std::is_same_v<T, SherpaTtsMatchaModel>) {
                requirePath(value.acoustic_model,
                            "Sherpa Matcha acoustic model path is required");
                requirePath(value.vocoder,
                            "Sherpa Matcha vocoder path is required");
                requirePath(value.tokens,
                            "Sherpa Matcha tokens path is required");
                require(!( !value.lexicon.empty() && !value.data_dir.empty()),
                        "Sherpa Matcha lexicon and data_dir are mutually exclusive in libaudition");
                requirePositive(value.noise_scale,
                                "Sherpa Matcha noise_scale must be positive");
                requirePositive(value.length_scale,
                                "Sherpa Matcha length_scale must be positive");
            } else {
                requirePath(value.model,
                            "Sherpa Kokoro model path is required");
                requirePath(value.voices,
                            "Sherpa Kokoro voices path is required");
                requirePath(value.tokens,
                            "Sherpa Kokoro tokens path is required");
                requirePath(value.data_dir,
                            "Sherpa Kokoro data_dir is required");
                require(!value.language.empty() || !value.lexicon.empty(),
                        "Sherpa Kokoro requires a language or lexicon");
                requirePositive(value.length_scale,
                                "Sherpa Kokoro length_scale must be positive");
            }
        },
        model);
}

}  // namespace

void validateSherpaTtsOptions(const SherpaTtsOptions& options) {
    validateDescriptor(options.model_descriptor);
    static_cast<void>(sherpa_detail::providerFor(options.runtime));
    validateModel(options.model);

    require(options.max_num_sentences > 0,
            "Sherpa TTS max_num_sentences must be positive");
    require(std::isfinite(options.silence_scale) &&
                options.silence_scale >= 0.0F,
            "Sherpa TTS silence_scale must be finite and non-negative");
    require(options.speaker_id >= 0,
            "Sherpa TTS speaker_id must be non-negative");

    require(std::all_of(options.languages.begin(), options.languages.end(),
                        [](const std::string& language) {
                            return !language.empty();
                        }),
            "Sherpa TTS language entries must be non-empty");
}

namespace sherpa_detail {

sherpa_onnx::cxx::OfflineTtsConfig makeTtsConfig(
    const SherpaTtsOptions& options) {
    validateSherpaTtsOptions(options);

    sherpa_onnx::cxx::OfflineTtsConfig config{};
    config.model.num_threads = options.runtime.num_threads;
    config.model.debug = options.runtime.debug;
    config.model.provider = providerFor(options.runtime);
    config.rule_fsts = resolved(options.rule_fsts, options.model_descriptor);
    config.rule_fars = resolved(options.rule_fars, options.model_descriptor);
    config.max_num_sentences = options.max_num_sentences;
    config.silence_scale = options.silence_scale;

    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SherpaTtsVitsModel>) {
                config.model.vits.model =
                    resolved(value.model, options.model_descriptor);
                config.model.vits.lexicon =
                    resolved(value.lexicon, options.model_descriptor);
                config.model.vits.tokens =
                    resolved(value.tokens, options.model_descriptor);
                config.model.vits.data_dir =
                    resolved(value.data_dir, options.model_descriptor);
                config.model.vits.noise_scale = value.noise_scale;
                config.model.vits.noise_scale_w = value.noise_scale_w;
                config.model.vits.length_scale = value.length_scale;
            } else if constexpr (std::is_same_v<T, SherpaTtsMatchaModel>) {
                config.model.matcha.acoustic_model =
                    resolved(value.acoustic_model, options.model_descriptor);
                config.model.matcha.vocoder =
                    resolved(value.vocoder, options.model_descriptor);
                config.model.matcha.lexicon =
                    resolved(value.lexicon, options.model_descriptor);
                config.model.matcha.tokens =
                    resolved(value.tokens, options.model_descriptor);
                config.model.matcha.data_dir =
                    resolved(value.data_dir, options.model_descriptor);
                config.model.matcha.noise_scale = value.noise_scale;
                config.model.matcha.length_scale = value.length_scale;
            } else {
                config.model.kokoro.model =
                    resolved(value.model, options.model_descriptor);
                config.model.kokoro.voices =
                    resolved(value.voices, options.model_descriptor);
                config.model.kokoro.tokens =
                    resolved(value.tokens, options.model_descriptor);
                config.model.kokoro.data_dir =
                    resolved(value.data_dir, options.model_descriptor);
                config.model.kokoro.lexicon =
                    resolved(value.lexicon, options.model_descriptor);
                config.model.kokoro.lang = value.language;
                config.model.kokoro.length_scale = value.length_scale;
            }
        },
        options.model);

    return config;
}

}  // namespace sherpa_detail
}  // namespace audition

#endif
