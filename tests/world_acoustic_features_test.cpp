#include <audition/backends/world.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace {

audition::AudioBuffer harmonicVoice(
    std::uint32_t sample_rate_hz,
    double fundamental_hz,
    double seconds) {
    constexpr double pi =
        3.14159265358979323846;

    const auto sample_count =
        static_cast<std::size_t>(
            static_cast<double>(sample_rate_hz) *
            seconds);

    std::vector<float> samples(sample_count);
    for (std::size_t i = 0U;
         i < samples.size();
         ++i) {
        const double t =
            static_cast<double>(i) /
            static_cast<double>(sample_rate_hz);
        const double value =
            0.55 * std::sin(
                2.0 * pi *
                fundamental_hz * t) +
            0.25 * std::sin(
                2.0 * pi *
                2.0 * fundamental_hz * t) +
            0.10 * std::sin(
                2.0 * pi *
                3.0 * fundamental_hz * t);

        samples[i] =
            static_cast<float>(value);
    }

    return {
        std::move(samples),
        {sample_rate_hz,
         1U,
         audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
}

double voicedMean(
    const std::vector<double>& f0) {
    double sum = 0.0;
    std::size_t count = 0U;

    for (const double value : f0) {
        if (value > 0.0) {
            sum += value;
            ++count;
        }
    }

    if (count == 0U) {
        return 0.0;
    }
    return sum /
           static_cast<double>(count);
}

}  // namespace

TEST(WorldAcousticConfig,
     AcceptsDefaultConfiguration) {
    EXPECT_NO_THROW(
        audition::validateWorldAcousticAnalysisOptions(
            audition::WorldAcousticAnalysisOptions{}));
}

TEST(WorldAcousticConfig,
     RejectsInvalidFeatureParameters) {
    auto options =
        audition::WorldAcousticAnalysisOptions{};

    options.d4c_threshold = -0.1;
    EXPECT_THROW(
        audition::validateWorldAcousticAnalysisOptions(
            options),
        audition::Error);

    options =
        audition::WorldAcousticAnalysisOptions{};
    options.d4c_threshold = 1.1;
    EXPECT_THROW(
        audition::validateWorldAcousticAnalysisOptions(
            options),
        audition::Error);

    options =
        audition::WorldAcousticAnalysisOptions{};
    options.cheaptrick_q1 =
        std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(
        audition::validateWorldAcousticAnalysisOptions(
            options),
        audition::Error);
}

TEST(WorldAcousticBackend,
     ImplementsAcousticAnalyzerContract) {
    static_assert(
        std::is_base_of_v<
            audition::IVoiceAcousticAnalyzer,
            audition::WorldAcousticAnalyzer>);
}

TEST(WorldAcousticBackend,
     ReportsFullWorldFeatureCapabilities) {
    const audition::WorldAcousticAnalyzer analyzer{};
    const auto capabilities =
        analyzer.capabilities();

    EXPECT_TRUE(capabilities.f0_contour);
    EXPECT_TRUE(capabilities.spectral_envelope);
    EXPECT_TRUE(capabilities.aperiodicity);
    EXPECT_EQ(capabilities.audio.min_channels, 1U);
    EXPECT_EQ(capabilities.audio.max_channels, 1U);
    EXPECT_TRUE(
        capabilities.audio.supported_sample_rates_hz.empty());
    ASSERT_EQ(
        capabilities.execution.device_classes.size(),
        1U);
    EXPECT_EQ(
        capabilities.execution.device_classes.front(),
        audition::DeviceClass::Cpu);
}

TEST(WorldAcousticBackend,
     ProducesConsistentFrameLevelFeatures) {
    audition::WorldAcousticAnalysisOptions options{};
    options.algorithm =
        audition::WorldF0Algorithm::DioStoneMask;
    options.f0_floor_hz = 80.0;
    options.f0_ceil_hz = 400.0;

    audition::WorldAcousticAnalyzer analyzer{options};
    auto audio =
        harmonicVoice(16000U, 220.0, 1.0);

    const auto features =
        analyzer.analyze(audio.view());

    EXPECT_EQ(features.sample_rate_hz, 16000U);
    EXPECT_DOUBLE_EQ(
        features.frame_period_ms, 5.0);
    EXPECT_GT(features.frame_count, 0U);
    EXPECT_GT(features.fft_size, 0U);
    EXPECT_EQ(
        features.frequency_bin_count,
        features.fft_size / 2U + 1U);

    EXPECT_EQ(
        features.time_axis_seconds.size(),
        features.frame_count);
    EXPECT_EQ(
        features.f0_hz.size(),
        features.frame_count);
    EXPECT_EQ(
        features.voiced_mask.size(),
        features.frame_count);
    for (std::size_t i = 0U;
         i < features.frame_count;
         ++i) {
        EXPECT_EQ(
            features.voiced_mask[i],
            features.f0_hz[i] > 0.0 ? 1U : 0U);
    }
    EXPECT_EQ(
        features.spectral_envelope.size(),
        features.frame_count *
            features.frequency_bin_count);
    EXPECT_EQ(
        features.aperiodicity.size(),
        features.frame_count *
            features.frequency_bin_count);

    EXPECT_TRUE(
        std::is_sorted(
            features.time_axis_seconds.begin(),
            features.time_axis_seconds.end()));

    EXPECT_NEAR(
        voicedMean(features.f0_hz),
        220.0,
        4.0);

    for (const double value :
         features.spectral_envelope) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GE(value, 0.0);
    }

    for (const double value :
         features.aperiodicity) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GE(value, 0.0);
    }
}

TEST(WorldAcousticBackend,
     RejectsAudioThatViolatesExplicitContract) {
    audition::WorldAcousticAnalysisOptions options{};
    options.f0_ceil_hz = 5000.0;
    audition::WorldAcousticAnalyzer analyzer{options};

    auto low_rate =
        harmonicVoice(8000U, 180.0, 0.5);

    EXPECT_THROW(
        static_cast<void>(
            analyzer.analyze(low_rate.view())),
        audition::Error);
}
