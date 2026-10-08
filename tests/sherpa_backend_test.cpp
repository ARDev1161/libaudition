#include <audition/backends/sherpa.hpp>

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
    audition::SherpaKeywordSpotterOptions options{};
    audition::SherpaOnlineTransducerModel model{};
    model.encoder = "encoder.onnx";
    model.decoder = "decoder.onnx";
    model.joiner = "joiner.onnx";
    options.model = model;
    options.tokens = "tokens.txt";

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
