#include "detail/sherpa_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include <audition/core/error.hpp>

namespace audition {
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

void requireFinite(float value, const char* message) {
    require(std::isfinite(value), message);
}

void requirePositive(float value, const char* message) {
    requireFinite(value, message);
    require(value > 0.0F, message);
}

void requireProbability(float value, const char* message) {
    requireFinite(value, message);
    require(value >= 0.0F && value <= 1.0F, message);
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

    if (runtime.execution.provider.empty()) {
        require(runtime.execution.device_class == DeviceClass::Auto ||
                    runtime.execution.device_class == DeviceClass::Cpu,
                "Non-CPU Sherpa execution targets require an explicit provider name");
    }
}

void validateFeatures(const SherpaFeatureOptions& features) {
    require(features.sample_rate_hz > 0U, "Sherpa sample rate must be non-zero");
    require(features.feature_dim > 0U, "Sherpa feature dimension must be non-zero");
    require(features.sample_rate_hz <=
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "Sherpa sample rate exceeds backend range");
    require(features.feature_dim <=
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "Sherpa feature dimension exceeds backend range");
}

void requirePath(const std::filesystem::path& path, const char* message) {
    require(!path.empty(), message);
}

template <typename... Paths>
void requirePaths(const Paths&... paths) {
    (requirePath(paths.first, paths.second), ...);
}

void validateOnlineModel(const SherpaOnlineModel& model) {
    std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SherpaOnlineTransducerModel>) {
                requirePaths(
                    std::pair{value.encoder, "Sherpa transducer encoder path is required"},
                    std::pair{value.decoder, "Sherpa transducer decoder path is required"},
                    std::pair{value.joiner, "Sherpa transducer joiner path is required"});
            } else if constexpr (std::is_same_v<T, SherpaOnlineParaformerModel>) {
                requirePaths(
                    std::pair{value.encoder, "Sherpa Paraformer encoder path is required"},
                    std::pair{value.decoder, "Sherpa Paraformer decoder path is required"});
            } else {
                requirePath(value.model, "Sherpa streaming model path is required");
            }
        },
        model);
}

void validateOfflineModel(const SherpaOfflineModel& model) {
    std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SherpaOfflineTransducerModel>) {
                requirePaths(
                    std::pair{value.encoder, "Sherpa transducer encoder path is required"},
                    std::pair{value.decoder, "Sherpa transducer decoder path is required"},
                    std::pair{value.joiner, "Sherpa transducer joiner path is required"});
            } else if constexpr (std::is_same_v<T, SherpaOfflineWhisperModel>) {
                requirePaths(
                    std::pair{value.encoder, "Sherpa Whisper encoder path is required"},
                    std::pair{value.decoder, "Sherpa Whisper decoder path is required"});
                require(value.task == "transcribe" || value.task == "translate",
                        "Sherpa Whisper task must be transcribe or translate");
            } else {
                requirePath(value.model, "Sherpa offline model path is required");
            }
        },
        model);
}

std::filesystem::path resolvePath(const std::filesystem::path& path,
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

template <typename Config>
void fillCommonOnlineModel(Config& config,
                           const SherpaOnlineModel& model,
                           const std::filesystem::path& tokens,
                           const std::filesystem::path& bpe_vocab,
                           const std::optional<ModelDescriptor>& descriptor,
                           const SherpaRuntimeOptions& runtime,
                           const std::string& model_type,
                           const std::string& modeling_unit) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SherpaOnlineTransducerModel>) {
                config.transducer.encoder = resolved(value.encoder, descriptor);
                config.transducer.decoder = resolved(value.decoder, descriptor);
                config.transducer.joiner = resolved(value.joiner, descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOnlineParaformerModel>) {
                config.paraformer.encoder = resolved(value.encoder, descriptor);
                config.paraformer.decoder = resolved(value.decoder, descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOnlineZipformer2CtcModel>) {
                config.zipformer2_ctc.model = resolved(value.model, descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOnlineNemoCtcModel>) {
                config.nemo_ctc.model = resolved(value.model, descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOnlineToneCtcModel>) {
                config.t_one_ctc.model = resolved(value.model, descriptor);
            }
        },
        model);

    config.tokens = resolved(tokens, descriptor);
    config.num_threads = runtime.num_threads;
    config.provider = sherpa_detail::providerFor(runtime);
    config.debug = runtime.debug;
    config.model_type = model_type;
    config.modeling_unit = modeling_unit;
    config.bpe_vocab = resolved(bpe_vocab, descriptor);
}

void fillHomophone(sherpa_onnx::cxx::HomophoneReplacerConfig& destination,
                   const SherpaHomophoneOptions& source,
                   const std::optional<ModelDescriptor>& descriptor) {
    destination.dict_dir = resolved(source.dict_dir, descriptor);
    destination.lexicon = resolved(source.lexicon, descriptor);
    destination.rule_fsts = resolved(source.rule_fsts, descriptor);
}

void validateCommonOnline(const SherpaOnlineModel& model,
                          const std::filesystem::path& tokens,
                          const SherpaRuntimeOptions& runtime,
                          const SherpaFeatureOptions& features,
                          const std::optional<ModelDescriptor>& descriptor) {
    validateDescriptor(descriptor);
    validateRuntime(runtime);
    validateFeatures(features);
    validateOnlineModel(model);
    requirePath(tokens, "Sherpa token path is required");
}

Duration secondsToDuration(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0F) {
        throw Error{ErrorCode::ProcessingError,
                    "Sherpa returned an invalid timestamp"};
    }
    const auto nanoseconds =
        static_cast<std::int64_t>(std::llround(static_cast<double>(seconds) * 1.0e9));
    return Duration{nanoseconds};
}

template <typename Result>
std::vector<TimedToken> timedTokens(const Result& result) {
    const auto count = std::min(result.tokens.size(), result.timestamps.size());
    std::vector<TimedToken> output;
    output.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        TimedToken token{};
        token.text = result.tokens[i];
        token.start_offset = secondsToDuration(result.timestamps[i]);
        if (i + 1U < count) {
            token.end_offset = secondsToDuration(result.timestamps[i + 1U]);
        }
        output.push_back(std::move(token));
    }
    return output;
}

}  // namespace

void validateSherpaOfflineAsrOptions(const SherpaOfflineAsrOptions& options) {
    validateDescriptor(options.model_descriptor);
    validateRuntime(options.runtime);
    validateFeatures(options.features);
    validateOfflineModel(options.model);
    requirePath(options.tokens, "Sherpa token path is required");
    require(options.max_active_paths > 0, "Sherpa max_active_paths must be positive");
    requirePositive(options.hotwords_score, "Sherpa hotwords_score must be positive");
    requireFinite(options.blank_penalty, "Sherpa blank_penalty must be finite");
    requirePositive(options.lm_scale, "Sherpa LM scale must be positive");
    require(options.ctc_fst.max_active > 0, "Sherpa CTC FST max_active must be positive");
}

void validateSherpaStreamingAsrOptions(const SherpaStreamingAsrOptions& options) {
    validateCommonOnline(options.model, options.tokens, options.runtime, options.features,
                         options.model_descriptor);
    require(options.max_active_paths > 0, "Sherpa max_active_paths must be positive");
    requirePositive(options.rule1_min_trailing_silence,
                    "Sherpa endpoint rule1 threshold must be positive");
    requirePositive(options.rule2_min_trailing_silence,
                    "Sherpa endpoint rule2 threshold must be positive");
    requirePositive(options.rule3_min_utterance_length,
                    "Sherpa endpoint rule3 threshold must be positive");
    requirePositive(options.hotwords_score, "Sherpa hotwords_score must be positive");
    requireFinite(options.blank_penalty, "Sherpa blank_penalty must be finite");
    require(options.ctc_fst.max_active > 0, "Sherpa CTC FST max_active must be positive");
}

void validateSherpaKeywordSpotterOptions(const SherpaKeywordSpotterOptions& options) {
    validateCommonOnline(options.model, options.tokens, options.runtime, options.features,
                         options.model_descriptor);
    require(options.max_active_paths > 0, "Sherpa KWS max_active_paths must be positive");
    require(options.num_trailing_blanks >= 0,
            "Sherpa KWS num_trailing_blanks must be non-negative");
    requirePositive(options.keywords_score, "Sherpa KWS keywords_score must be positive");
    requireProbability(options.keywords_threshold,
                       "Sherpa KWS keywords_threshold must be in [0, 1]");
    require(!options.keywords_file.empty() || !options.keywords.empty(),
            "Sherpa KWS requires keywords_file or inline keywords");
}

void validateSherpaVadOptions(const SherpaVadOptions& options) {
    validateDescriptor(options.model_descriptor);
    validateRuntime(options.runtime);
    require(options.sample_rate_hz > 0U, "Sherpa VAD sample rate must be non-zero");
    require(options.sample_rate_hz <=
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "Sherpa VAD sample rate exceeds backend range");
    requirePositive(options.buffer_size_seconds,
                    "Sherpa VAD buffer_size_seconds must be positive");

    std::visit(
        [](const auto& model) {
            requirePath(model.model, "Sherpa VAD model path is required");
            requireProbability(model.threshold, "Sherpa VAD threshold must be in [0, 1]");
            require(model.min_silence_duration >= 0.0F &&
                        std::isfinite(model.min_silence_duration),
                    "Sherpa VAD minimum silence duration must be finite and non-negative");
            require(model.min_speech_duration >= 0.0F &&
                        std::isfinite(model.min_speech_duration),
                    "Sherpa VAD minimum speech duration must be finite and non-negative");
            require(model.window_size > 0, "Sherpa VAD window size must be positive");
            requirePositive(model.max_speech_duration,
                            "Sherpa VAD maximum speech duration must be positive");
        },
        options.model);
}

void validateSherpaLanguageIdOptions(const SherpaLanguageIdOptions& options) {
    validateDescriptor(options.model_descriptor);
    validateRuntime(options.runtime);
    requirePath(options.encoder, "Sherpa language-ID encoder path is required");
    requirePath(options.decoder, "Sherpa language-ID decoder path is required");
    require(options.sample_rate_hz > 0U, "Sherpa language-ID sample rate must be non-zero");
    require(options.sample_rate_hz <=
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "Sherpa language-ID sample rate exceeds backend range");
    require(options.tail_paddings >= 0,
            "Sherpa language-ID tail_paddings must be non-negative");
}

namespace sherpa_detail {

std::string providerFor(const SherpaRuntimeOptions& runtime) {
    validateRuntime(runtime);
    if (!runtime.execution.provider.empty()) {
        return runtime.execution.provider;
    }
    return "cpu";
}

sherpa_onnx::cxx::OfflineRecognizerConfig makeOfflineAsrConfig(
    const SherpaOfflineAsrOptions& options) {
    validateSherpaOfflineAsrOptions(options);
    sherpa_onnx::cxx::OfflineRecognizerConfig config{};

    config.feat_config.sample_rate = static_cast<std::int32_t>(options.features.sample_rate_hz);
    config.feat_config.feature_dim = static_cast<std::int32_t>(options.features.feature_dim);
    config.model_config.tokens = resolved(options.tokens, options.model_descriptor);
    config.model_config.num_threads = options.runtime.num_threads;
    config.model_config.provider = providerFor(options.runtime);
    config.model_config.debug = options.runtime.debug;
    config.model_config.model_type = options.model_type;
    config.model_config.modeling_unit = options.modeling_unit;
    config.model_config.bpe_vocab = resolved(options.bpe_vocab, options.model_descriptor);

    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SherpaOfflineTransducerModel>) {
                config.model_config.transducer.encoder =
                    resolved(value.encoder, options.model_descriptor);
                config.model_config.transducer.decoder =
                    resolved(value.decoder, options.model_descriptor);
                config.model_config.transducer.joiner =
                    resolved(value.joiner, options.model_descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOfflineParaformerModel>) {
                config.model_config.paraformer.model =
                    resolved(value.model, options.model_descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOfflineNemoCtcModel>) {
                config.model_config.nemo_ctc.model =
                    resolved(value.model, options.model_descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOfflineWhisperModel>) {
                config.model_config.whisper.encoder =
                    resolved(value.encoder, options.model_descriptor);
                config.model_config.whisper.decoder =
                    resolved(value.decoder, options.model_descriptor);
                config.model_config.whisper.language = value.language;
                config.model_config.whisper.task = value.task;
                config.model_config.whisper.tail_paddings = value.tail_paddings;
                config.model_config.whisper.enable_token_timestamps =
                    value.enable_token_timestamps;
                config.model_config.whisper.enable_segment_timestamps =
                    value.enable_segment_timestamps;
            } else if constexpr (std::is_same_v<T, SherpaOfflineSenseVoiceModel>) {
                config.model_config.sense_voice.model =
                    resolved(value.model, options.model_descriptor);
                config.model_config.sense_voice.language = value.language;
                config.model_config.sense_voice.use_itn = value.use_itn;
            } else if constexpr (std::is_same_v<T, SherpaOfflineZipformerCtcModel>) {
                config.model_config.zipformer_ctc.model =
                    resolved(value.model, options.model_descriptor);
            } else if constexpr (std::is_same_v<T, SherpaOfflineWenetCtcModel>) {
                config.model_config.wenet_ctc.model =
                    resolved(value.model, options.model_descriptor);
            }
        },
        options.model);

    config.lm_config.model = resolved(options.lm_model, options.model_descriptor);
    config.lm_config.scale = options.lm_scale;
    config.decoding_method = options.decoding_method;
    config.max_active_paths = options.max_active_paths;
    config.hotwords_file = resolved(options.hotwords_file, options.model_descriptor);
    config.hotwords_score = options.hotwords_score;
    config.rule_fsts = resolved(options.rule_fsts, options.model_descriptor);
    config.rule_fars = resolved(options.rule_fars, options.model_descriptor);
    config.blank_penalty = options.blank_penalty;
    config.ctc_fst_decoder_config.graph =
        resolved(options.ctc_fst.graph, options.model_descriptor);
    config.ctc_fst_decoder_config.max_active = options.ctc_fst.max_active;
    fillHomophone(config.hr, options.homophone, options.model_descriptor);
    return config;
}

sherpa_onnx::cxx::OnlineRecognizerConfig makeStreamingAsrConfig(
    const SherpaStreamingAsrOptions& options) {
    validateSherpaStreamingAsrOptions(options);
    sherpa_onnx::cxx::OnlineRecognizerConfig config{};

    config.feat_config.sample_rate = static_cast<std::int32_t>(options.features.sample_rate_hz);
    config.feat_config.feature_dim = static_cast<std::int32_t>(options.features.feature_dim);
    fillCommonOnlineModel(config.model_config, options.model, options.tokens,
                          options.bpe_vocab, options.model_descriptor, options.runtime,
                          options.model_type, options.modeling_unit);

    config.decoding_method = options.decoding_method;
    config.max_active_paths = options.max_active_paths;
    config.enable_endpoint = options.enable_endpoint;
    config.rule1_min_trailing_silence = options.rule1_min_trailing_silence;
    config.rule2_min_trailing_silence = options.rule2_min_trailing_silence;
    config.rule3_min_utterance_length = options.rule3_min_utterance_length;
    config.hotwords_file = resolved(options.hotwords_file, options.model_descriptor);
    config.hotwords_score = options.hotwords_score;
    config.hotwords_buf = options.hotwords;
    config.rule_fsts = resolved(options.rule_fsts, options.model_descriptor);
    config.rule_fars = resolved(options.rule_fars, options.model_descriptor);
    config.blank_penalty = options.blank_penalty;
    config.ctc_fst_decoder_config.graph =
        resolved(options.ctc_fst.graph, options.model_descriptor);
    config.ctc_fst_decoder_config.max_active = options.ctc_fst.max_active;
    fillHomophone(config.hr, options.homophone, options.model_descriptor);
    return config;
}

sherpa_onnx::cxx::KeywordSpotterConfig makeKeywordSpotterConfig(
    const SherpaKeywordSpotterOptions& options) {
    validateSherpaKeywordSpotterOptions(options);
    sherpa_onnx::cxx::KeywordSpotterConfig config{};
    config.feat_config.sample_rate = static_cast<std::int32_t>(options.features.sample_rate_hz);
    config.feat_config.feature_dim = static_cast<std::int32_t>(options.features.feature_dim);
    fillCommonOnlineModel(config.model_config, options.model, options.tokens,
                          options.bpe_vocab, options.model_descriptor, options.runtime,
                          options.model_type, options.modeling_unit);
    config.max_active_paths = options.max_active_paths;
    config.num_trailing_blanks = options.num_trailing_blanks;
    config.keywords_score = options.keywords_score;
    config.keywords_threshold = options.keywords_threshold;
    config.keywords_file = resolved(options.keywords_file, options.model_descriptor);
    config.keywords_buf = options.keywords;
    return config;
}

sherpa_onnx::cxx::VadModelConfig makeVadConfig(const SherpaVadOptions& options) {
    validateSherpaVadOptions(options);
    sherpa_onnx::cxx::VadModelConfig config{};
    config.sample_rate = static_cast<std::int32_t>(options.sample_rate_hz);
    config.num_threads = options.runtime.num_threads;
    config.provider = providerFor(options.runtime);
    config.debug = options.runtime.debug;

    std::visit(
        [&](const auto& model) {
            using T = std::decay_t<decltype(model)>;
            if constexpr (std::is_same_v<T, SherpaSileroVadModel>) {
                config.silero_vad.model = resolved(model.model, options.model_descriptor);
                config.silero_vad.threshold = model.threshold;
                config.silero_vad.min_silence_duration = model.min_silence_duration;
                config.silero_vad.min_speech_duration = model.min_speech_duration;
                config.silero_vad.window_size = model.window_size;
                config.silero_vad.max_speech_duration = model.max_speech_duration;
            } else {
                config.ten_vad.model = resolved(model.model, options.model_descriptor);
                config.ten_vad.threshold = model.threshold;
                config.ten_vad.min_silence_duration = model.min_silence_duration;
                config.ten_vad.min_speech_duration = model.min_speech_duration;
                config.ten_vad.window_size = model.window_size;
                config.ten_vad.max_speech_duration = model.max_speech_duration;
            }
        },
        options.model);
    return config;
}

sherpa_onnx::cxx::SpokenLanguageIdentificationConfig makeLanguageIdConfig(
    const SherpaLanguageIdOptions& options) {
    validateSherpaLanguageIdOptions(options);
    sherpa_onnx::cxx::SpokenLanguageIdentificationConfig config{};
    config.whisper.encoder = resolved(options.encoder, options.model_descriptor);
    config.whisper.decoder = resolved(options.decoder, options.model_descriptor);
    config.whisper.tail_paddings = options.tail_paddings;
    config.num_threads = options.runtime.num_threads;
    config.provider = providerFor(options.runtime);
    config.debug = options.runtime.debug;
    return config;
}

AudioRequirements monoRequirements(std::uint32_t sample_rate_hz) {
    AudioRequirements requirements{};
    requirements.supported_sample_rates_hz = {sample_rate_hz};
    requirements.min_channels = 1U;
    requirements.max_channels = 1U;
    requirements.supported_layouts = {AudioLayout::Interleaved, AudioLayout::Planar};
    return requirements;
}

void validateMonoAudio(AudioView audio, std::uint32_t sample_rate_hz, const char* role) {
    if (!audio.format().valid()) {
        throw Error{ErrorCode::InvalidArgument,
                    std::string{"Sherpa "} + role + " requires valid audio"};
    }
    if (audio.format().channel_count != 1U ||
        audio.format().sample_rate_hz != sample_rate_hz) {
        throw Error{ErrorCode::UnsupportedFormat,
                    std::string{"Sherpa "} + role +
                        " requires mono audio at its configured sample rate"};
    }
}

Transcript transcriptFromOffline(
    const sherpa_onnx::cxx::OfflineRecognizerResult& result,
    SpeechSegmentId segment_id) {
    Transcript transcript{};
    transcript.segment_id = segment_id;
    transcript.text = result.text;
    transcript.language = result.lang;
    transcript.tokens = timedTokens(result);

    const auto count = std::min(
        {transcript.tokens.size(), result.timestamps.size(), result.durations.size()});
    for (std::size_t i = 0; i < count; ++i) {
        if (std::isfinite(result.durations[i]) && result.durations[i] >= 0.0F) {
            transcript.tokens[i].end_offset =
                transcript.tokens[i].start_offset.advancedBy(
                    secondsToDuration(result.durations[i]));
        }
    }
    return transcript;
}

Transcript transcriptFromOnline(
    const sherpa_onnx::cxx::OnlineRecognizerResult& result) {
    Transcript transcript{};
    transcript.text = result.text;
    transcript.tokens = timedTokens(result);
    return transcript;
}

bool offlineTokenTimestampsConfigured(const SherpaOfflineAsrOptions& options) {
    const auto* whisper = std::get_if<SherpaOfflineWhisperModel>(&options.model);
    return whisper != nullptr && whisper->enable_token_timestamps;
}

bool offlineLanguageIdentificationExpected(const SherpaOfflineAsrOptions& options) {
    return std::holds_alternative<SherpaOfflineWhisperModel>(options.model) ||
           std::holds_alternative<SherpaOfflineSenseVoiceModel>(options.model);
}

}  // namespace sherpa_detail
}  // namespace audition
