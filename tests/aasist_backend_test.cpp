#include <audition/backends/aasist.hpp>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace {

audition::AasistOnnxOptions validOptions() {
    audition::AasistOnnxOptions options{};
    options.model = "aasist.onnx";
    return options;
}

audition::AudioBuffer makeSine(
    std::size_t frame_count,
    std::uint32_t sample_rate_hz = 16000U) {
    constexpr double pi =
        3.14159265358979323846;
    constexpr double frequency_hz = 440.0;

    std::vector<float> samples(frame_count);
    for (std::size_t i = 0U;
         i < samples.size();
         ++i) {
        const double phase =
            2.0 * pi * frequency_hz *
            static_cast<double>(i) /
            static_cast<double>(sample_rate_hz);
        samples[i] =
            static_cast<float>(
                0.1 * std::sin(phase));
    }

    return {
        std::move(samples),
        {sample_rate_hz, 1U,
         audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
}

double sigmoid(double value) {
    if (value >= 0.0) {
        return 1.0 / (1.0 + std::exp(-value));
    }
    const double e = std::exp(value);
    return e / (1.0 + e);
}

}  // namespace

TEST(AasistConfig, AcceptsOfficialModelContract) {
    const auto options = validOptions();
    EXPECT_NO_THROW(
        audition::validateAasistOnnxOptions(
            options));
}

TEST(AasistConfig, RejectsMissingModelAndInvalidRuntime) {
    auto options = validOptions();
    options.model.clear();
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);

    options = validOptions();
    options.intra_op_threads = 0;
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);

    options = validOptions();
    options.sample_rate_hz = 48000U;
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);

    options = validOptions();
    options.frame_count = 64000U;
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);
}

TEST(AasistConfig, RejectsUnsupportedExecutionTarget) {
    auto options = validOptions();
    options.execution.device_class =
        audition::DeviceClass::Npu;
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);

    options = validOptions();
    options.execution.provider = "rknn";
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);

    options = validOptions();
    options.execution.precision =
        audition::PrecisionPreference::Float16;
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);
}

TEST(AasistConfig, ValidatesOptionalCalibration) {
    auto options = validOptions();
    options.calibration =
        audition::AasistPlattCalibration{
            1.5, -0.25};
    EXPECT_NO_THROW(
        audition::validateAasistOnnxOptions(
            options));

    options.calibration =
        audition::AasistPlattCalibration{
            0.0, 0.0};
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);

    options.calibration =
        audition::AasistPlattCalibration{
            std::numeric_limits<double>::infinity(),
            0.0};
    EXPECT_THROW(
        audition::validateAasistOnnxOptions(
            options),
        audition::Error);
}

TEST(AasistBackend, ImplementsAuthenticityContract) {
    static_assert(
        std::is_base_of_v<
            audition::IAudioAuthenticityDetector,
            audition::AasistAuthenticityDetector>);
}

TEST(AasistBackend, RealModelInferenceWhenAvailable) {
    const char* model_path =
        std::getenv(
            "LIBAUDITION_TEST_AASIST_MODEL");
    if (model_path == nullptr ||
        *model_path == '\0') {
        GTEST_SKIP()
            << "CI-only AASIST model fixture is not configured";
    }

    auto options = validOptions();
    options.model = model_path;

    audition::AasistAuthenticityDetector detector{
        options};

    const auto capabilities =
        detector.capabilities();
    EXPECT_FALSE(
        capabilities.calibrated_probability);
    EXPECT_FALSE(
        capabilities.replay_attribution);
    EXPECT_FALSE(
        capabilities.synthetic_attribution);
    ASSERT_EQ(
        capabilities.audio.supported_sample_rates_hz.size(),
        1U);
    EXPECT_EQ(
        capabilities.audio.supported_sample_rates_hz.front(),
        16000U);
    ASSERT_TRUE(
        capabilities.audio.preferred_frame_count.has_value());
    EXPECT_EQ(
        *capabilities.audio.preferred_frame_count,
        64600U);

    auto short_audio =
        makeSine(16000U);
    const auto result =
        detector.analyze(short_audio.view());

    ASSERT_TRUE(
        result.bona_fide_score.has_value());
    ASSERT_TRUE(
        result.spoof_score.has_value());
    EXPECT_TRUE(std::isfinite(
        result.bona_fide_score->value));
    EXPECT_TRUE(std::isfinite(
        result.spoof_score->value));
    EXPECT_FALSE(
        result.bona_fide_probability.has_value());
    EXPECT_FALSE(
        result.spoof_probability.has_value());
    EXPECT_FALSE(
        result.replay_probability.has_value());
    EXPECT_FALSE(
        result.synthetic_probability.has_value());

    auto calibrated_options = options;
    calibrated_options.calibration =
        audition::AasistPlattCalibration{
            1.25, -0.1};

    audition::AasistAuthenticityDetector calibrated{
        calibrated_options};
    const auto calibrated_result =
        calibrated.analyze(short_audio.view());

    ASSERT_TRUE(
        calibrated_result.bona_fide_score.has_value());
    ASSERT_TRUE(
        calibrated_result.bona_fide_probability.has_value());
    ASSERT_TRUE(
        calibrated_result.spoof_probability.has_value());

    const double expected =
        sigmoid(
            1.25 *
                calibrated_result
                    .bona_fide_score->value -
            0.1);

    EXPECT_NEAR(
        calibrated_result
            .bona_fide_probability->value(),
        expected,
        1.0e-12);
    EXPECT_NEAR(
        calibrated_result
            .spoof_probability->value(),
        1.0 - expected,
        1.0e-12);

    auto wrong_rate =
        makeSine(16000U, 48000U);
    EXPECT_THROW(
        static_cast<void>(
            detector.analyze(
                wrong_rate.view())),
        audition::Error);

    auto too_long =
        makeSine(64601U);
    EXPECT_THROW(
        static_cast<void>(
            detector.analyze(
                too_long.view())),
        audition::Error);
}
