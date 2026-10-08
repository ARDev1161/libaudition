#include <audition/backends/clap.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "detail/clap_features.hpp"

namespace {

audition::ClapOnnxAudioOptions validOptions() {
    audition::ClapOnnxAudioOptions options{};
    options.model = "clap_audio.onnx";
    return options;
}

}  // namespace

TEST(ClapConfig, AcceptsCpuAudioEmbeddingConfiguration) {
    const auto options = validOptions();
    EXPECT_NO_THROW(audition::validateClapOnnxAudioOptions(options));
}

TEST(ClapConfig, RejectsMissingModelAndIdentity) {
    auto options = validOptions();
    options.model.clear();
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);

    options = validOptions();
    options.model_id.clear();
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);
}

TEST(ClapConfig, RejectsInvalidThreadsRateAndDimension) {
    auto options = validOptions();
    options.intra_op_threads = 0;
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);

    options = validOptions();
    options.sample_rate_hz = 0U;
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);

    options = validOptions();
    options.embedding_dimension = 0U;
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);
}

TEST(ClapConfig, RejectsUnsupportedExecutionKnobs) {
    auto options = validOptions();
    options.execution.device_class = audition::DeviceClass::Npu;
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);

    options = validOptions();
    options.execution.provider = "rknn";
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);

    options = validOptions();
    options.execution.device_index = 0;
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);

    options = validOptions();
    options.execution.precision =
        audition::PrecisionPreference::Float16;
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);

    options = validOptions();
    options.execution.provider_options["arena"] = "1";
    EXPECT_THROW(audition::validateClapOnnxAudioOptions(options),
                 audition::Error);
}

TEST(ClapConfig, AcceptsDescriptorModelRoot) {
    auto options = validOptions();
    audition::ModelDescriptor descriptor{};
    descriptor.backend = "clap-onnx";
    descriptor.artifact_path = "/opt/models/clap";
    options.model_descriptor = descriptor;
    EXPECT_NO_THROW(audition::validateClapOnnxAudioOptions(options));
}

TEST(ClapBackend, ImplementsAudioEmbedderContract) {
    static_assert(std::is_base_of_v<audition::IAudioEmbedder,
                                    audition::ClapAudioEmbedder>);
}

TEST(ClapFrontend, UnitSineMatchesReferenceDbRange) {
    constexpr std::size_t sample_count = 480000U;
    constexpr double pi = 3.14159265358979323846;
    constexpr double frequency_hz = 1000.0;
    constexpr double sample_rate_hz = 48000.0;

    std::vector<float> samples(sample_count);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double phase =
            2.0 * pi * frequency_hz *
            static_cast<double>(i) / sample_rate_hz;
        samples[i] = static_cast<float>(std::sin(phase));
    }

    audition::AudioBuffer audio{
        std::move(samples),
        {48000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};

    audition::clap_detail::ClapFeatureExtractor extractor;
    const auto features = extractor.extract(audio.view());

    EXPECT_EQ(features.frames, 1001U);
    EXPECT_EQ(features.mel_bins, 64U);
    ASSERT_EQ(features.values.size(), 1001U * 64U);

    const auto bounds =
        std::minmax_element(features.values.begin(), features.values.end());
    ASSERT_NE(bounds.first, features.values.end());
    ASSERT_NE(bounds.second, features.values.end());
    EXPECT_NEAR(*bounds.second, 29.3F, 1.0F);
    EXPECT_NEAR(*bounds.first, -100.0F, 1.0F);
}

TEST(ClapBackend, LoadsPinnedAudioFixtureAndEmbedsWhenAvailable) {
    const char* model_path =
        std::getenv("LIBAUDITION_TEST_CLAP_AUDIO_MODEL");
    if (model_path == nullptr || *model_path == '\0') {
        GTEST_SKIP() << "CI-only CLAP audio model fixture is not configured";
    }

    auto options = validOptions();
    options.model = model_path;
    audition::ClapAudioEmbedder embedder{options};

    const auto capabilities = embedder.capabilities();
    ASSERT_TRUE(capabilities.embedding_dimension.has_value());
    EXPECT_EQ(*capabilities.embedding_dimension, 512U);
    ASSERT_EQ(capabilities.audio.supported_sample_rates_hz.size(), 1U);
    EXPECT_EQ(capabilities.audio.supported_sample_rates_hz.front(), 48000U);

    const std::size_t frame_count =
        capabilities.audio.preferred_frame_count.value_or(480000U);
    std::vector<float> samples(frame_count);
    constexpr double pi = 3.14159265358979323846;
    constexpr double frequency_hz = 440.0;
    constexpr double sample_rate_hz = 48000.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double phase =
            2.0 * pi * frequency_hz *
            static_cast<double>(i) / sample_rate_hz;
        samples[i] =
            static_cast<float>(0.1 * std::sin(phase));
    }

    audition::AudioBuffer audio{
        std::move(samples),
        {48000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};

    const auto embedding = embedder.embed(audio.view());
    EXPECT_EQ(embedding.model_id, options.model_id);
    EXPECT_EQ(embedding.values.size(), 512U);
    EXPECT_FALSE(embedding.quality.has_value());

    audition::AudioBuffer wrong_rate{
        std::vector<float>(16000U, 0.0F),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
    EXPECT_THROW(
        static_cast<void>(embedder.embed(wrong_rate.view())),
        audition::Error);
}
