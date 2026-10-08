#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <audition/core/execution.hpp>
#include <audition/core/export.hpp>
#include <audition/model/model_descriptor.hpp>

namespace audition {

struct SherpaRuntimeOptions {
    std::int32_t num_threads{1};
    bool debug{false};
    ExecutionTarget execution{};
};

struct SherpaFeatureOptions {
    std::uint32_t sample_rate_hz{16000};
    std::uint32_t feature_dim{80};
};

struct SherpaHomophoneOptions {
    std::filesystem::path dict_dir{};
    std::filesystem::path lexicon{};
    std::filesystem::path rule_fsts{};
};

struct SherpaCtcFstOptions {
    std::filesystem::path graph{};
    std::int32_t max_active{3000};
};

struct SherpaOnlineTransducerModel {
    std::filesystem::path encoder{};
    std::filesystem::path decoder{};
    std::filesystem::path joiner{};
};

struct SherpaOnlineParaformerModel {
    std::filesystem::path encoder{};
    std::filesystem::path decoder{};
};

struct SherpaOnlineZipformer2CtcModel {
    std::filesystem::path model{};
};

struct SherpaOnlineNemoCtcModel {
    std::filesystem::path model{};
};

struct SherpaOnlineToneCtcModel {
    std::filesystem::path model{};
};

using SherpaOnlineModel = std::variant<
    SherpaOnlineTransducerModel,
    SherpaOnlineParaformerModel,
    SherpaOnlineZipformer2CtcModel,
    SherpaOnlineNemoCtcModel,
    SherpaOnlineToneCtcModel>;

struct SherpaOfflineTransducerModel {
    std::filesystem::path encoder{};
    std::filesystem::path decoder{};
    std::filesystem::path joiner{};
};

struct SherpaOfflineParaformerModel {
    std::filesystem::path model{};
};

struct SherpaOfflineNemoCtcModel {
    std::filesystem::path model{};
};

struct SherpaOfflineWhisperModel {
    std::filesystem::path encoder{};
    std::filesystem::path decoder{};
    std::string language{};
    std::string task{"transcribe"};
    std::int32_t tail_paddings{-1};
    bool enable_token_timestamps{false};
};

struct SherpaOfflineSenseVoiceModel {
    std::filesystem::path model{};
    std::string language{"auto"};
    bool use_itn{false};
};

struct SherpaOfflineZipformerCtcModel {
    std::filesystem::path model{};
};

struct SherpaOfflineWenetCtcModel {
    std::filesystem::path model{};
};

using SherpaOfflineModel = std::variant<
    SherpaOfflineTransducerModel,
    SherpaOfflineParaformerModel,
    SherpaOfflineNemoCtcModel,
    SherpaOfflineWhisperModel,
    SherpaOfflineSenseVoiceModel,
    SherpaOfflineZipformerCtcModel,
    SherpaOfflineWenetCtcModel>;

struct SherpaOfflineAsrOptions {
    SherpaOfflineModel model{SherpaOfflineWhisperModel{}};
    std::filesystem::path tokens{};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    SherpaFeatureOptions features{};

    std::string model_type{};
    std::string modeling_unit{"cjkchar"};
    std::filesystem::path bpe_vocab{};
    std::string decoding_method{"greedy_search"};
    std::int32_t max_active_paths{4};

    std::filesystem::path hotwords_file{};
    std::string hotwords{};
    float hotwords_score{1.5F};
    std::filesystem::path rule_fsts{};
    std::filesystem::path rule_fars{};
    float blank_penalty{0.0F};

    std::filesystem::path lm_model{};
    float lm_scale{1.0F};
    SherpaCtcFstOptions ctc_fst{};
    SherpaHomophoneOptions homophone{};
};

struct SherpaStreamingAsrOptions {
    SherpaOnlineModel model{SherpaOnlineTransducerModel{}};
    std::filesystem::path tokens{};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    SherpaFeatureOptions features{};

    std::string model_type{};
    std::string modeling_unit{"cjkchar"};
    std::filesystem::path bpe_vocab{};
    std::string decoding_method{"greedy_search"};
    std::int32_t max_active_paths{4};

    bool enable_endpoint{false};
    float rule1_min_trailing_silence{2.4F};
    float rule2_min_trailing_silence{1.2F};
    float rule3_min_utterance_length{20.0F};

    std::filesystem::path hotwords_file{};
    std::string hotwords{};
    float hotwords_score{1.5F};
    std::filesystem::path rule_fsts{};
    std::filesystem::path rule_fars{};
    float blank_penalty{0.0F};
    SherpaCtcFstOptions ctc_fst{};
    SherpaHomophoneOptions homophone{};
};

struct SherpaKeywordSpotterOptions {
    SherpaOnlineModel model{SherpaOnlineTransducerModel{}};
    std::filesystem::path tokens{};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    SherpaFeatureOptions features{};

    std::string model_type{};
    std::string modeling_unit{"cjkchar"};
    std::filesystem::path bpe_vocab{};
    std::int32_t max_active_paths{4};
    std::int32_t num_trailing_blanks{1};
    float keywords_score{1.0F};
    float keywords_threshold{0.25F};
    std::filesystem::path keywords_file{};
    std::string keywords{};
};

struct SherpaSileroVadModel {
    std::filesystem::path model{};
    float threshold{0.5F};
    float min_silence_duration{0.5F};
    float min_speech_duration{0.25F};
    std::int32_t window_size{512};
    float max_speech_duration{20.0F};
};

struct SherpaTenVadModel {
    std::filesystem::path model{};
    float threshold{0.5F};
    float min_silence_duration{0.5F};
    float min_speech_duration{0.25F};
    std::int32_t window_size{256};
    float max_speech_duration{20.0F};
};

using SherpaVadModel = std::variant<SherpaSileroVadModel, SherpaTenVadModel>;

struct SherpaVadOptions {
    SherpaVadModel model{SherpaSileroVadModel{}};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    std::uint32_t sample_rate_hz{16000};
    float buffer_size_seconds{30.0F};
};

struct SherpaLanguageIdOptions {
    std::filesystem::path encoder{};
    std::filesystem::path decoder{};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    std::uint32_t sample_rate_hz{16000};
    std::int32_t tail_paddings{0};
};

struct SherpaAudioTaggingZipformerModel {
    std::filesystem::path model{};
};

struct SherpaAudioTaggingCedModel {
    std::filesystem::path model{};
};

using SherpaAudioTaggingModel =
    std::variant<SherpaAudioTaggingZipformerModel, SherpaAudioTaggingCedModel>;

struct SherpaAudioTaggingOptions {
    SherpaAudioTaggingModel model{SherpaAudioTaggingZipformerModel{}};
    std::filesystem::path labels{};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    std::uint32_t sample_rate_hz{16000};
    std::int32_t top_k{5};
};

struct SherpaSpeechDenoiserGtcrnModel {
    std::filesystem::path model{};
};

struct SherpaSpeechDenoiserDpdfNetModel {
    std::filesystem::path model{};
    float attenuation_limit_db{0.0F};
};

using SherpaSpeechDenoiserModel =
    std::variant<SherpaSpeechDenoiserGtcrnModel, SherpaSpeechDenoiserDpdfNetModel>;

struct SherpaSpeechDenoiserOptions {
    SherpaSpeechDenoiserModel model{SherpaSpeechDenoiserGtcrnModel{}};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
};

#if defined(LIBAUDITION_SHERPA_TTS_ENABLED)
struct SherpaTtsVitsModel {
    std::filesystem::path model{};
    std::filesystem::path lexicon{};
    std::filesystem::path tokens{};
    std::filesystem::path data_dir{};
    float noise_scale{0.667F};
    float noise_scale_w{0.8F};
    float length_scale{1.0F};
};

struct SherpaTtsMatchaModel {
    std::filesystem::path acoustic_model{};
    std::filesystem::path vocoder{};
    std::filesystem::path lexicon{};
    std::filesystem::path tokens{};
    std::filesystem::path data_dir{};
    float noise_scale{0.667F};
    float length_scale{1.0F};
};

struct SherpaTtsKokoroModel {
    std::filesystem::path model{};
    std::filesystem::path voices{};
    std::filesystem::path tokens{};
    std::filesystem::path data_dir{};
    std::filesystem::path lexicon{};
    std::string language{};
    float length_scale{1.0F};
};

using SherpaTtsModel =
    std::variant<SherpaTtsVitsModel, SherpaTtsMatchaModel, SherpaTtsKokoroModel>;

struct SherpaTtsOptions {
    SherpaTtsModel model{SherpaTtsVitsModel{}};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    std::filesystem::path rule_fsts{};
    std::filesystem::path rule_fars{};
    std::int32_t max_num_sentences{1};
    float silence_scale{0.2F};
    std::int32_t speaker_id{0};
    std::vector<std::string> languages{};
};

AUDITION_API void validateSherpaTtsOptions(const SherpaTtsOptions& options);
#endif

struct SherpaSpeakerEmbeddingOptions {
    std::filesystem::path model{};
    std::optional<ModelDescriptor> model_descriptor{};
    SherpaRuntimeOptions runtime{};
    std::uint32_t sample_rate_hz{16000};
    std::string model_id{};
};

struct SherpaSpeakerVerifierOptions {
    bool require_same_model_id{true};
};

struct SherpaSpeakerIdentifierOptions {
    std::size_t embedding_dimension{0};
    std::string model_id{};
};

struct SherpaSpeakerDiarizationOptions {
    std::filesystem::path segmentation_model{};
    std::optional<ModelDescriptor> segmentation_model_descriptor{};
    SherpaRuntimeOptions segmentation_runtime{};
    float segmentation_window_shift_ratio{0.1F};

    std::filesystem::path embedding_model{};
    std::optional<ModelDescriptor> embedding_model_descriptor{};
    SherpaRuntimeOptions embedding_runtime{};

    std::int32_t num_clusters{0};
    float clustering_threshold{0.5F};
    bool compute_confidence{false};
    float min_duration_on{0.0F};
    float min_duration_off{0.0F};
};

AUDITION_API void validateSherpaAudioTaggingOptions(const SherpaAudioTaggingOptions& options);
AUDITION_API void validateSherpaOfflineSpeechDenoiserOptions(
    const SherpaSpeechDenoiserOptions& options);
AUDITION_API void validateSherpaStreamingSpeechDenoiserOptions(
    const SherpaSpeechDenoiserOptions& options);
AUDITION_API void validateSherpaSpeakerEmbeddingOptions(const SherpaSpeakerEmbeddingOptions& options);
AUDITION_API void validateSherpaSpeakerIdentifierOptions(const SherpaSpeakerIdentifierOptions& options);
AUDITION_API void validateSherpaSpeakerDiarizationOptions(const SherpaSpeakerDiarizationOptions& options);

AUDITION_API void validateSherpaOfflineAsrOptions(const SherpaOfflineAsrOptions& options);
AUDITION_API void validateSherpaStreamingAsrOptions(const SherpaStreamingAsrOptions& options);
AUDITION_API void validateSherpaKeywordSpotterOptions(const SherpaKeywordSpotterOptions& options);
AUDITION_API void validateSherpaVadOptions(const SherpaVadOptions& options);
AUDITION_API void validateSherpaLanguageIdOptions(const SherpaLanguageIdOptions& options);

}  // namespace audition
