#include <audition/backends/sherpa.hpp>

#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

namespace {

audition::SherpaOfflineAsrOptions whisperOptions() {
    audition::SherpaOfflineAsrOptions options{};
    audition::SherpaOfflineWhisperModel model{};
    model.encoder = "encoder.onnx";
    model.decoder = "decoder.onnx";
    model.enable_token_timestamps = true;
    options.model = model;
    options.tokens = "tokens.txt";
    return options;
}

audition::SherpaStreamingAsrOptions streamingOptions() {
    audition::SherpaStreamingAsrOptions options{};
    audition::SherpaOnlineTransducerModel model{};
    model.encoder = "encoder.onnx";
    model.decoder = "decoder.onnx";
    model.joiner = "joiner.onnx";
    options.model = model;
    options.tokens = "tokens.txt";
    return options;
}

audition::SherpaAudioTaggingOptions audioTaggingOptions() {
    audition::SherpaAudioTaggingOptions options{};
    std::get<audition::SherpaAudioTaggingZipformerModel>(options.model).model =
        "audio-tagger.onnx";
    options.labels = "labels.csv";
    return options;
}

audition::SherpaKeywordSpotterOptions keywordSpotterOptions() {
    audition::SherpaKeywordSpotterOptions options{};
    audition::SherpaOnlineTransducerModel model{};
    model.encoder = "encoder.onnx";
    model.decoder = "decoder.onnx";
    model.joiner = "joiner.onnx";
    options.model = model;
    options.tokens = "tokens.txt";
    return options;
}

audition::SherpaSpeechDenoiserOptions speechDenoiserOptions() {
    audition::SherpaSpeechDenoiserOptions options{};
    std::get<audition::SherpaSpeechDenoiserGtcrnModel>(options.model).model =
        "gtcrn.onnx";
    return options;
}

#if defined(LIBAUDITION_SHERPA_TTS_ENABLED)
audition::SherpaTtsOptions piperTtsOptions() {
    audition::SherpaTtsOptions options{};
    auto& model = std::get<audition::SherpaTtsVitsModel>(options.model);
    model.model = "voice.onnx";
    model.tokens = "tokens.txt";
    model.data_dir = "espeak-ng-data";
    options.languages = {"en"};
    return options;
}
#endif

}  // namespace

TEST(SherpaConfig, AcceptsOfflineWhisperConfiguration) {
    auto options = whisperOptions();
    EXPECT_NO_THROW(audition::validateSherpaOfflineAsrOptions(options));
}

TEST(SherpaConfig, RejectsMissingOfflineModelFile) {
    auto options = whisperOptions();
    auto model = std::get<audition::SherpaOfflineWhisperModel>(options.model);
    model.encoder.clear();
    options.model = model;
    EXPECT_THROW(audition::validateSherpaOfflineAsrOptions(options), audition::Error);
}

TEST(SherpaConfig, RequiresExplicitProviderForGenericAccelerator) {
    auto options = streamingOptions();
    options.runtime.execution.device_class = audition::DeviceClass::Npu;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);

    options.runtime.execution.provider = "rknn";
    EXPECT_NO_THROW(audition::validateSherpaStreamingAsrOptions(options));
}

TEST(SherpaConfig, RejectsUnsupportedExecutionKnobsRatherThanIgnoringThem) {
    auto options = streamingOptions();
    options.runtime.execution.precision = audition::PrecisionPreference::Float16;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);

    options = streamingOptions();
    options.runtime.execution.allow_fallback = false;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
}

TEST(SherpaConfig, KeywordSpotterRequiresKeywordDefinitions) {
    auto options = keywordSpotterOptions();
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);

    options.keywords = "HELLO :2.0";
    EXPECT_NO_THROW(audition::validateSherpaKeywordSpotterOptions(options));
}

TEST(SherpaConfig, SupportsSileroAndTenVadModels) {
    audition::SherpaVadOptions silero{};
    std::get<audition::SherpaSileroVadModel>(silero.model).model = "silero.onnx";
    EXPECT_NO_THROW(audition::validateSherpaVadOptions(silero));

    audition::SherpaVadOptions ten{};
    audition::SherpaTenVadModel ten_model{};
    ten_model.model = "ten-vad.onnx";
    ten.model = ten_model;
    EXPECT_NO_THROW(audition::validateSherpaVadOptions(ten));
}

TEST(SherpaConfig, AudioTaggingSupportsZipformerAndCed) {
    auto options = audioTaggingOptions();
    EXPECT_NO_THROW(audition::validateSherpaAudioTaggingOptions(options));

    audition::SherpaAudioTaggingCedModel ced{};
    ced.model = "ced.onnx";
    options.model = ced;
    EXPECT_NO_THROW(audition::validateSherpaAudioTaggingOptions(options));
}

TEST(SherpaConfig, AudioTaggingRejectsMissingModelAndLabels) {
    auto options = audioTaggingOptions();
    std::get<audition::SherpaAudioTaggingZipformerModel>(options.model).model.clear();
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);

    options = audioTaggingOptions();
    options.labels.clear();
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);
}

TEST(SherpaConfig, AudioTaggingRejectsInvalidTopKAndSampleRate) {
    auto options = audioTaggingOptions();
    options.top_k = 0;
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);

    options = audioTaggingOptions();
    options.sample_rate_hz = 0U;
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);

    options = audioTaggingOptions();
    options.sample_rate_hz = 48000U;
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);
}

TEST(SherpaConfig, AudioTaggingRequiresExplicitProviderForAccelerator) {
    auto options = audioTaggingOptions();
    options.runtime.execution.device_class = audition::DeviceClass::Npu;
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);

    options.runtime.execution.provider = "rknn";
    EXPECT_NO_THROW(audition::validateSherpaAudioTaggingOptions(options));
}

TEST(SherpaConfig, AudioTaggingRejectsUnsupportedExecutionKnobs) {
    auto options = audioTaggingOptions();
    options.runtime.execution.precision = audition::PrecisionPreference::Float16;
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);

    options = audioTaggingOptions();
    options.runtime.execution.allow_fallback = false;
    EXPECT_THROW(audition::validateSherpaAudioTaggingOptions(options), audition::Error);
}

TEST(SherpaConfig, AudioTaggingAcceptsDescriptorModelRoot) {
    auto options = audioTaggingOptions();
    audition::ModelDescriptor descriptor{};
    descriptor.backend = "sherpa-onnx";
    descriptor.artifact_path = "/opt/models/audio-tagging";
    options.model_descriptor = descriptor;
    EXPECT_NO_THROW(audition::validateSherpaAudioTaggingOptions(options));
}

TEST(SherpaConfig, SpeechDenoiserSupportsGtcrnOfflineAndStreaming) {
    const auto options = speechDenoiserOptions();
    EXPECT_NO_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options));
    EXPECT_NO_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options));
}

TEST(SherpaConfig, SpeechDenoiserSupportsDpdfNetOfflineAttenuation) {
    auto options = speechDenoiserOptions();
    audition::SherpaSpeechDenoiserDpdfNetModel model{};
    model.model = "dpdfnet.onnx";
    model.attenuation_limit_db = 12.0F;
    options.model = model;

    EXPECT_NO_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options));
    EXPECT_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options),
                 audition::Error);

    model.attenuation_limit_db = 0.0F;
    options.model = model;
    EXPECT_NO_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options));
}

TEST(SherpaConfig, SpeechDenoiserRejectsMissingModelAndInvalidAttenuation) {
    auto options = speechDenoiserOptions();
    std::get<audition::SherpaSpeechDenoiserGtcrnModel>(options.model).model.clear();
    EXPECT_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options),
                 audition::Error);

    audition::SherpaSpeechDenoiserDpdfNetModel dpdfnet{};
    dpdfnet.model = "dpdfnet.onnx";
    dpdfnet.attenuation_limit_db = -1.0F;
    options.model = dpdfnet;
    EXPECT_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options),
                 audition::Error);
}

TEST(SherpaConfig, SpeechDenoiserRequiresExplicitProviderForAccelerator) {
    auto options = speechDenoiserOptions();
    options.runtime.execution.device_class = audition::DeviceClass::Npu;
    EXPECT_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options),
                 audition::Error);

    options.runtime.execution.provider = "rknn";
    EXPECT_NO_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options));
}

TEST(SherpaConfig, SpeechDenoiserRejectsUnsupportedExecutionKnobs) {
    auto options = speechDenoiserOptions();
    options.runtime.execution.precision = audition::PrecisionPreference::Float16;
    EXPECT_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options),
                 audition::Error);

    options = speechDenoiserOptions();
    options.runtime.execution.allow_fallback = false;
    EXPECT_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options),
                 audition::Error);
}

TEST(SherpaConfig, SpeechDenoiserAcceptsDescriptorModelRoot) {
    auto options = speechDenoiserOptions();
    audition::ModelDescriptor descriptor{};
    descriptor.backend = "sherpa-onnx";
    descriptor.artifact_path = "/opt/models/denoiser";
    options.model_descriptor = descriptor;
    EXPECT_NO_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options));
    EXPECT_NO_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options));
}

#if defined(LIBAUDITION_SHERPA_TTS_ENABLED)
TEST(SherpaConfig, TtsSupportsPiperVitsMatchaAndKokoro) {
    auto options = piperTtsOptions();
    EXPECT_NO_THROW(audition::validateSherpaTtsOptions(options));

    audition::SherpaTtsMatchaModel matcha{};
    matcha.acoustic_model = "matcha.onnx";
    matcha.vocoder = "vocoder.onnx";
    matcha.tokens = "tokens.txt";
    matcha.lexicon = "lexicon.txt";
    options.model = matcha;
    EXPECT_NO_THROW(audition::validateSherpaTtsOptions(options));

    audition::SherpaTtsKokoroModel kokoro{};
    kokoro.model = "kokoro.onnx";
    kokoro.voices = "voices.bin";
    kokoro.tokens = "tokens.txt";
    kokoro.data_dir = "espeak-ng-data";
    kokoro.language = "en";
    options.model = kokoro;
    EXPECT_NO_THROW(audition::validateSherpaTtsOptions(options));
}

TEST(SherpaConfig, TtsRejectsMissingPathsAndConflictingFrontendResources) {
    auto options = piperTtsOptions();
    auto model = std::get<audition::SherpaTtsVitsModel>(options.model);
    model.model.clear();
    options.model = model;
    EXPECT_THROW(audition::validateSherpaTtsOptions(options), audition::Error);

    options = piperTtsOptions();
    model = std::get<audition::SherpaTtsVitsModel>(options.model);
    model.lexicon = "lexicon.txt";
    options.model = model;
    EXPECT_THROW(audition::validateSherpaTtsOptions(options), audition::Error);
}

TEST(SherpaConfig, TtsRejectsInvalidGenerationAndExecutionSettings) {
    auto options = piperTtsOptions();
    options.silence_scale = -0.1F;
    EXPECT_THROW(audition::validateSherpaTtsOptions(options), audition::Error);

    options = piperTtsOptions();
    options.speaker_id = -1;
    EXPECT_THROW(audition::validateSherpaTtsOptions(options), audition::Error);

    options = piperTtsOptions();
    options.runtime.execution.device_class = audition::DeviceClass::Npu;
    EXPECT_THROW(audition::validateSherpaTtsOptions(options), audition::Error);
    options.runtime.execution.provider = "rknn";
    EXPECT_NO_THROW(audition::validateSherpaTtsOptions(options));
}

TEST(SherpaConfig, TtsAcceptsDescriptorModelRoot) {
    auto options = piperTtsOptions();
    audition::ModelDescriptor descriptor{};
    descriptor.backend = "sherpa-onnx";
    descriptor.artifact_path = "/opt/models/piper";
    options.model_descriptor = descriptor;
    EXPECT_NO_THROW(audition::validateSherpaTtsOptions(options));
}
#endif

TEST(SherpaConfig, LanguageIdRequiresWhisperPair) {
    audition::SherpaLanguageIdOptions options{};
    options.encoder = "encoder.onnx";
    EXPECT_THROW(audition::validateSherpaLanguageIdOptions(options), audition::Error);
    options.decoder = "decoder.onnx";
    EXPECT_NO_THROW(audition::validateSherpaLanguageIdOptions(options));
}

TEST(SherpaConfig, DescriptorCanProvideModelRoot) {
    auto options = whisperOptions();
    audition::ModelDescriptor descriptor{};
    descriptor.backend = "sherpa-onnx";
    descriptor.artifact_path = "/opt/models/whisper";
    options.model_descriptor = descriptor;
    EXPECT_NO_THROW(audition::validateSherpaOfflineAsrOptions(options));
}


TEST(SherpaVad, LoadsPinnedSileroFixtureAndProcessesAudioWhenAvailable) {
    const char* model_path = std::getenv("LIBAUDITION_TEST_SILERO_MODEL");
    if (model_path == nullptr || *model_path == '\0') {
        GTEST_SKIP() << "CI-only Silero model fixture is not configured";
    }

    audition::SherpaVadOptions options{};
    auto& model = std::get<audition::SherpaSileroVadModel>(options.model);
    model.model = model_path;

    audition::SherpaVad vad{options};
    const auto requirements = vad.audioRequirements();
    ASSERT_EQ(requirements.supported_sample_rates_hz.size(), 1U);
    EXPECT_EQ(requirements.supported_sample_rates_hz.front(), 16000U);
    ASSERT_TRUE(requirements.preferred_frame_count.has_value());
    EXPECT_EQ(*requirements.preferred_frame_count, 512U);

    auto session = vad.createSession();
    audition::AudioBuffer silence{
        std::vector<float>(512U, 0.0F),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};

    for (int i = 0; i < 10; ++i) {
        const auto result = session->process(silence.view());
        EXPECT_FALSE(result.speech_probability.has_value());
    }
    EXPECT_NO_THROW(session->reset());
}
