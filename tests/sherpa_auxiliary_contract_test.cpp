#include <audition/backends/sherpa.hpp>

#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

audition::SherpaStreamingAsrOptions streaming() {
    audition::SherpaStreamingAsrOptions options{};
    options.model = audition::SherpaOnlineTransducerModel{
        "encoder.onnx", "decoder.onnx", "joiner.onnx"};
    options.tokens = "tokens.txt";
    return options;
}

audition::SherpaKeywordSpotterOptions kws() {
    audition::SherpaKeywordSpotterOptions options{};
    options.model = audition::SherpaOnlineTransducerModel{
        "encoder.onnx", "decoder.onnx", "joiner.onnx"};
    options.tokens = "tokens.txt";
    options.keywords = "HELLO :2.0";
    return options;
}

audition::SherpaSpeechDenoiserOptions denoiser() {
    audition::SherpaSpeechDenoiserOptions options{};
    options.model = audition::SherpaSpeechDenoiserGtcrnModel{"gtcrn.onnx"};
    return options;
}

audition::SherpaSpeakerDiarizationOptions diarization() {
    audition::SherpaSpeakerDiarizationOptions options{};
    options.segmentation_model = "segmentation.onnx";
    options.embedding_model = "embedding.onnx";
    return options;
}

std::vector<audition::SherpaOnlineModel> onlineModelVariants() {
    return {
        audition::SherpaOnlineTransducerModel{
            "encoder.onnx", "decoder.onnx", "joiner.onnx"},
        audition::SherpaOnlineParaformerModel{"encoder.onnx", "decoder.onnx"},
        audition::SherpaOnlineZipformer2CtcModel{"zipformer2.onnx"},
        audition::SherpaOnlineNemoCtcModel{"nemo_ctc.onnx"},
        audition::SherpaOnlineToneCtcModel{"t_one_ctc.onnx"},
    };
}

}  // namespace

TEST(SherpaOnlineAsrContract, ValidatesAllFiveOnlineModelFamilies) {
    for (const auto& model : onlineModelVariants()) {
        auto options = streaming();
        options.model = model;
        EXPECT_NO_THROW(audition::validateSherpaStreamingAsrOptions(options));
    }
}

TEST(SherpaOnlineAsrContract, RejectsIncompleteModelVariantsAndMissingTokens) {
    auto options = streaming();
    options.tokens.clear();
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);

    options = streaming();
    options.model = audition::SherpaOnlineParaformerModel{"encoder.onnx", {}};
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);

    options.model = audition::SherpaOnlineZipformer2CtcModel{};
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);

    options.model = audition::SherpaOnlineNemoCtcModel{};
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);

    options.model = audition::SherpaOnlineToneCtcModel{};
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
}

TEST(SherpaOnlineAsrContract, RejectsInvalidEndpointAndDecoderConstraints) {
    auto options = streaming();
    options.rule1_min_trailing_silence = 0.0F;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.rule2_min_trailing_silence = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.rule3_min_utterance_length = -1.0F;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.max_active_paths = 0;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.hotwords_score = std::numeric_limits<float>::infinity();
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.blank_penalty = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.ctc_fst.max_active = 0;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
}

TEST(SherpaOnlineAsrContract, RejectsInvalidFeatureAndRuntimeSettings) {
    auto options = streaming();
    options.features.sample_rate_hz = 0;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.features.feature_dim = 0;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
    options = streaming();
    options.runtime.num_threads = 0;
    EXPECT_THROW(audition::validateSherpaStreamingAsrOptions(options), audition::Error);
}

TEST(SherpaKwsContract, ValidatesAllFiveOnlineModelFamilies) {
    for (const auto& model : onlineModelVariants()) {
        auto options = kws();
        options.model = model;
        EXPECT_NO_THROW(audition::validateSherpaKeywordSpotterOptions(options));
    }
}

TEST(SherpaKwsContract, RejectsInvalidKeywordsAndDetectionThresholds) {
    auto options = kws();
    options.keywords.clear();
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);
    options = kws();
    options.num_trailing_blanks = -1;
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);
    options = kws();
    options.max_active_paths = 0;
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);
    options = kws();
    options.keywords_score = 0.0F;
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);
    options = kws();
    options.keywords_threshold = -0.1F;
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);
    options = kws();
    options.keywords_threshold = 1.1F;
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);
    options = kws();
    options.keywords_threshold = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(audition::validateSherpaKeywordSpotterOptions(options), audition::Error);
}

TEST(SherpaLanguageIdContract, AcceptsValidWhisperPairAndBoundaryTailPadding) {
    audition::SherpaLanguageIdOptions options{};
    options.encoder = "encoder.onnx";
    options.decoder = "decoder.onnx";
    options.tail_paddings = 0;
    EXPECT_NO_THROW(audition::validateSherpaLanguageIdOptions(options));
    options.tail_paddings = 10;
    EXPECT_NO_THROW(audition::validateSherpaLanguageIdOptions(options));
}

TEST(SherpaLanguageIdContract, RejectsIncompleteModelsAndInvalidRuntime) {
    audition::SherpaLanguageIdOptions options{};
    options.encoder = "encoder.onnx";
    EXPECT_THROW(audition::validateSherpaLanguageIdOptions(options), audition::Error);
    options.decoder = "decoder.onnx";
    options.encoder.clear();
    EXPECT_THROW(audition::validateSherpaLanguageIdOptions(options), audition::Error);
    options.encoder = "encoder.onnx";
    options.tail_paddings = -1;
    EXPECT_THROW(audition::validateSherpaLanguageIdOptions(options), audition::Error);
    options.tail_paddings = 0;
    options.sample_rate_hz = 0;
    EXPECT_THROW(audition::validateSherpaLanguageIdOptions(options), audition::Error);
    options.sample_rate_hz = 16000;
    options.runtime.num_threads = 0;
    EXPECT_THROW(audition::validateSherpaLanguageIdOptions(options), audition::Error);
}

TEST(SherpaDenoiserContract, ValidatesGtcrnAndDpdfNetModes) {
    auto options = denoiser();
    EXPECT_NO_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options));
    EXPECT_NO_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options));

    options.model = audition::SherpaSpeechDenoiserDpdfNetModel{"dpdfnet.onnx", 0.0F};
    EXPECT_NO_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options));
    EXPECT_NO_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options));
    options.model = audition::SherpaSpeechDenoiserDpdfNetModel{"dpdfnet.onnx", 12.0F};
    EXPECT_NO_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options));
    EXPECT_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options), audition::Error);
}

TEST(SherpaDenoiserContract, RejectsNonfiniteAndMissingModels) {
    auto options = denoiser();
    options.model = audition::SherpaSpeechDenoiserGtcrnModel{};
    EXPECT_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options), audition::Error);
    options.model = audition::SherpaSpeechDenoiserDpdfNetModel{
        "dpdfnet.onnx", std::numeric_limits<float>::quiet_NaN()};
    EXPECT_THROW(audition::validateSherpaOfflineSpeechDenoiserOptions(options), audition::Error);
    options.model = audition::SherpaSpeechDenoiserDpdfNetModel{
        "dpdfnet.onnx", std::numeric_limits<float>::infinity()};
    EXPECT_THROW(audition::validateSherpaStreamingSpeechDenoiserOptions(options), audition::Error);
}

TEST(SherpaDiarizationContract, RejectsInvalidClusteringAndTimingConfiguration) {
    auto options = diarization();
    EXPECT_NO_THROW(audition::validateSherpaSpeakerDiarizationOptions(options));
    options.segmentation_window_shift_ratio = 1.1F;
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options = diarization();
    options.segmentation_window_shift_ratio = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options = diarization();
    options.num_clusters = -1;
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options = diarization();
    options.clustering_threshold = 0.0F;
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options = diarization();
    options.min_duration_on = -1.0F;
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options = diarization();
    options.min_duration_off = std::numeric_limits<float>::infinity();
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
}

TEST(SherpaDiarizationContract, RejectsMissingWeightsAndBadProviders) {
    auto options = diarization();
    options.segmentation_model.clear();
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options = diarization();
    options.embedding_model.clear();
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options = diarization();
    options.segmentation_runtime.execution.device_class = audition::DeviceClass::Npu;
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
    options.segmentation_runtime.execution.provider = "rknn";
    EXPECT_NO_THROW(audition::validateSherpaSpeakerDiarizationOptions(options));
}

TEST(SherpaSpeakerEmbeddingContract, RejectsUnspecifiedModelAndSampleRate) {
    audition::SherpaSpeakerEmbeddingOptions options{};
    EXPECT_THROW(audition::validateSherpaSpeakerEmbeddingOptions(options), audition::Error);
    options.model = "speaker.onnx";
    EXPECT_NO_THROW(audition::validateSherpaSpeakerEmbeddingOptions(options));
    options.sample_rate_hz = 0;
    EXPECT_THROW(audition::validateSherpaSpeakerEmbeddingOptions(options), audition::Error);
}

