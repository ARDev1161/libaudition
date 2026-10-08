#include <audition/backends/world.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
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

}  // namespace

TEST(WorldConfig, AcceptsDefaultConfiguration) {
    EXPECT_NO_THROW(
        audition::validateWorldVoiceTraitsOptions(
            audition::WorldVoiceTraitsOptions{}));
}

TEST(WorldConfig, RejectsInvalidParameters) {
    auto options =
        audition::WorldVoiceTraitsOptions{};
    options.frame_period_ms = 0.0;
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);

    options =
        audition::WorldVoiceTraitsOptions{};
    options.f0_floor_hz = 0.0;
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);

    options =
        audition::WorldVoiceTraitsOptions{};
    options.f0_ceil_hz =
        options.f0_floor_hz;
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);

    options =
        audition::WorldVoiceTraitsOptions{};
    options.dio_speed = 13;
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);

    options =
        audition::WorldVoiceTraitsOptions{};
    options.dio_allowed_range =
        std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);

    options =
        audition::WorldVoiceTraitsOptions{};
    options.minimum_voiced_frames = 0U;
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);
}

TEST(WorldConfig, RejectsUnsupportedExecutionTarget) {
    auto options =
        audition::WorldVoiceTraitsOptions{};
    options.execution.device_class =
        audition::DeviceClass::Gpu;
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);

    options =
        audition::WorldVoiceTraitsOptions{};
    options.execution.provider = "cuda";
    EXPECT_THROW(
        audition::validateWorldVoiceTraitsOptions(
            options),
        audition::Error);
}

TEST(WorldBackend, ImplementsVoiceTraitsContract) {
    static_assert(
        std::is_base_of_v<
            audition::IVoiceTraitsEstimator,
            audition::WorldVoiceTraitsEstimator>);
}

TEST(WorldBackend, ReportsOnlySupportedTraits) {
    const audition::WorldVoiceTraitsEstimator estimator{};
    const auto capabilities =
        estimator.capabilities();

    EXPECT_TRUE(capabilities.pitch_statistics);
    EXPECT_FALSE(capabilities.estimated_age);
    EXPECT_FALSE(capabilities.categorical_traits);
    EXPECT_FALSE(capabilities.speaking_rate);
    EXPECT_EQ(capabilities.audio.min_channels, 1U);
    EXPECT_EQ(capabilities.audio.max_channels, 1U);
    EXPECT_TRUE(
        capabilities.audio.supported_sample_rates_hz.empty());
}

TEST(WorldBackend, DioStoneMaskTracksHarmonicPitch) {
    audition::WorldVoiceTraitsOptions options{};
    options.algorithm =
        audition::WorldF0Algorithm::DioStoneMask;
    options.f0_floor_hz = 80.0;
    options.f0_ceil_hz = 400.0;

    audition::WorldVoiceTraitsEstimator estimator{
        options};
    auto audio =
        harmonicVoice(16000U, 220.0, 2.0);

    const auto traits =
        estimator.estimate(audio.view());

    ASSERT_TRUE(
        traits.pitch_mean_hz.has_value());
    ASSERT_TRUE(
        traits.pitch_stddev_hz.has_value());
    EXPECT_NEAR(
        *traits.pitch_mean_hz,
        220.0,
        3.0);
    EXPECT_LT(
        *traits.pitch_stddev_hz,
        6.0);
    EXPECT_FALSE(
        traits.estimated_age_years.has_value());
    EXPECT_TRUE(
        traits.categorical_traits.empty());
    EXPECT_FALSE(
        traits.speaking_rate_syllables_per_second.has_value());
}

TEST(WorldBackend, HarvestTracksHarmonicPitch) {
    audition::WorldVoiceTraitsOptions options{};
    options.algorithm =
        audition::WorldF0Algorithm::Harvest;
    options.f0_floor_hz = 80.0;
    options.f0_ceil_hz = 400.0;

    audition::WorldVoiceTraitsEstimator estimator{
        options};
    auto audio =
        harmonicVoice(16000U, 180.0, 2.0);

    const auto traits =
        estimator.estimate(audio.view());

    ASSERT_TRUE(
        traits.pitch_mean_hz.has_value());
    ASSERT_TRUE(
        traits.pitch_stddev_hz.has_value());
    EXPECT_NEAR(
        *traits.pitch_mean_hz,
        180.0,
        5.0);
    EXPECT_LT(
        *traits.pitch_stddev_hz,
        10.0);
}

TEST(WorldBackend, SilenceProducesNoPitchEstimate) {
    std::vector<float> samples(32000U, 0.0F);
    audition::AudioBuffer silence{
        std::move(samples),
        {16000U,
         1U,
         audition::AudioLayout::Interleaved},
        audition::Timestamp{}};

    const audition::WorldVoiceTraitsEstimator estimator{};
    const auto traits =
        estimator.estimate(silence.view());

    EXPECT_FALSE(
        traits.pitch_mean_hz.has_value());
    EXPECT_FALSE(
        traits.pitch_stddev_hz.has_value());
}

TEST(WorldBackend, RejectsInvalidAudioContracts) {
    const audition::WorldVoiceTraitsEstimator estimator{};

    std::vector<float> stereo_samples(
        32000U, 0.0F);
    audition::AudioBuffer stereo{
        std::move(stereo_samples),
        {16000U,
         2U,
         audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
    EXPECT_THROW(
        static_cast<void>(
            estimator.estimate(
                stereo.view())),
        audition::Error);

    std::vector<float> bad_samples(
        16000U, 0.0F);
    bad_samples[10U] =
        std::numeric_limits<float>::quiet_NaN();
    audition::AudioBuffer bad{
        std::move(bad_samples),
        {16000U,
         1U,
         audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
    EXPECT_THROW(
        static_cast<void>(
            estimator.estimate(
                bad.view())),
        audition::Error);

    auto low_rate =
        harmonicVoice(1000U, 100.0, 1.0);
    EXPECT_THROW(
        static_cast<void>(
            estimator.estimate(
                low_rate.view())),
        audition::Error);
}
