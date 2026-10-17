#include <audition/dsp/quality.hpp>

#include <cmath>
#include <limits>
#include <cstddef>
#include <audition/dsp/basic.hpp>

#include <gtest/gtest.h>

TEST(AudioQuality, ComputesMeasuredSignalMetrics) {
    audition::AudioBuffer input{{-1.0F, 0.0F, 1.0F, 0.0F},
                                {16000U, 1U, audition::AudioLayout::Interleaved},
                                audition::Timestamp{}};
    const auto report = audition::dsp::analyzeQuality(input.view(), 0.99);
    ASSERT_EQ(report.channels.size(), 1U);
    EXPECT_NEAR(report.aggregate.rms_linear, std::sqrt(0.5), 1.0e-12);
    EXPECT_NEAR(report.aggregate.peak_linear, 1.0, 1.0e-12);
    EXPECT_NEAR(report.aggregate.dc_offset, 0.0, 1.0e-12);
    EXPECT_NEAR(report.aggregate.clipping_ratio, 0.5, 1.0e-12);
    EXPECT_NEAR(report.aggregate.crest_factor_db, 3.0102999566, 1.0e-9);
}

TEST(AudioQuality, SNRRequiresExplicitNoiseLevel) {
    EXPECT_NEAR(audition::dsp::snrDb(0.5, 0.05), 20.0, 1.0e-12);
    EXPECT_THROW(
        {
            const auto value = audition::dsp::snrDb(0.5, 0.0);
            (void)value;
        },
        audition::Error);
}

TEST(AudioQuality, SilenceAndEmptyBufferHaveWellDefinedDbfs) {
    audition::AudioBuffer empty{{},
        {16000U, 2U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto report = audition::dsp::analyzeQuality(empty.view());
    ASSERT_EQ(report.channels.size(), 2U);
    EXPECT_EQ(report.aggregate.rms_linear, 0.0);
    EXPECT_TRUE(std::isinf(report.aggregate.rms_dbfs));
    EXPECT_LT(report.aggregate.rms_dbfs, 0.0);
    for (const auto& channel : report.channels) {
        EXPECT_EQ(channel.peak_linear, 0.0);
        EXPECT_TRUE(std::isinf(channel.peak_dbfs));
        EXPECT_LT(channel.peak_dbfs, 0.0);
    }

    audition::AudioBuffer zeros{{0.F, 0.F, 0.F, 0.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto silent = audition::dsp::signalMetrics(zeros.view());
    EXPECT_EQ(silent.crest_factor_db, 0.0);
    EXPECT_EQ(silent.clipping_ratio, 0.0);
    EXPECT_LT(silent.rms_dbfs, 0.0);
    EXPECT_TRUE(std::isinf(silent.rms_dbfs));
}

TEST(AudioQuality, HonorsPlanarChannelsAndClippingThresholdBounds) {
    audition::AudioBuffer planar{{0.F, 1.F, -1.F, 0.F},
        {48000U, 2U, audition::AudioLayout::Planar}, audition::Timestamp{}};
    const auto report = audition::dsp::analyzeQuality(planar.view(), 0.9);
    ASSERT_EQ(report.channels.size(), 2U);
    EXPECT_DOUBLE_EQ(report.channels[0].peak_linear, 1.0);
    EXPECT_DOUBLE_EQ(report.channels[1].peak_linear, 1.0);
    EXPECT_DOUBLE_EQ(report.channels[0].clipping_ratio, 0.5);
    EXPECT_DOUBLE_EQ(report.channels[1].clipping_ratio, 0.5);
    EXPECT_DOUBLE_EQ(report.aggregate.clipping_ratio, 0.5);
    const auto clampLow = audition::dsp::channelSignalMetrics(planar.view(), 0, -5.0);
    const auto clampHigh = audition::dsp::channelSignalMetrics(planar.view(), 0, 5.0);
    EXPECT_DOUBLE_EQ(clampLow.clipping_ratio, 1.0);
    EXPECT_DOUBLE_EQ(clampHigh.clipping_ratio, 0.5);
    EXPECT_THROW(static_cast<void>(audition::dsp::channelSignalMetrics(planar.view(), 2)),
                 audition::Error);
}

TEST(AudioQuality, SnrRejectsInvalidRmsAndAcceptsSilentSignal) {
    using audition::dsp::snrDb;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double bad : {nan, inf, -1.0}) {
        EXPECT_THROW(static_cast<void>(snrDb(bad, 0.1)), audition::Error);
    }
    for (const double bad : {nan, inf, -0.1, 0.0}) {
        EXPECT_THROW(static_cast<void>(snrDb(1.0, bad)), audition::Error);
    }
    const auto zero = snrDb(0.0, 0.1);
    EXPECT_TRUE(std::isinf(zero));
    EXPECT_LT(zero, 0.0);
    EXPECT_NEAR(snrDb(0.2, 0.1), 20.0 * std::log10(2.0), 1.0e-12);
}


TEST(AudioQuality, ConstantSignalHasZeroCrestFactorAndExactDc) {
    audition::AudioBuffer audio{{0.25F, 0.25F, 0.25F, 0.25F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto metrics = audition::dsp::signalMetrics(audio.view(), 0.5);
    EXPECT_DOUBLE_EQ(metrics.dc_offset, 0.25);
    EXPECT_DOUBLE_EQ(metrics.rms_linear, 0.25);
    EXPECT_DOUBLE_EQ(metrics.peak_linear, 0.25);
    EXPECT_NEAR(metrics.rms_dbfs, 20.0 * std::log10(0.25), 1e-12);
    EXPECT_DOUBLE_EQ(metrics.crest_factor_db, 0.0);
    EXPECT_DOUBLE_EQ(metrics.clipping_ratio, 0.0);
}

TEST(AudioQuality, OppositeDcOffsetsCancelOnlyInAggregate) {
    audition::AudioBuffer audio{{1.F, -1.F, 1.F, -1.F},
        {16000U, 2U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto report = audition::dsp::analyzeQuality(audio.view());
    ASSERT_EQ(report.channels.size(), 2U);
    EXPECT_DOUBLE_EQ(report.aggregate.dc_offset, 0.0);
    EXPECT_DOUBLE_EQ(report.channels[0].dc_offset, 1.0);
    EXPECT_DOUBLE_EQ(report.channels[1].dc_offset, -1.0);
    EXPECT_DOUBLE_EQ(report.channels[0].rms_linear, 1.0);
    EXPECT_DOUBLE_EQ(report.channels[1].rms_linear, 1.0);
}

TEST(AudioQuality, AggregateMetricsAreLayoutInvariant) {
    audition::AudioBuffer interleaved{{1.F, 0.F, 0.5F, -0.5F},
        {16000U, 2U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto planar = audition::dsp::convertLayout(
        interleaved.view(), audition::AudioLayout::Planar);
    const auto first = audition::dsp::analyzeQuality(interleaved.view(), 0.5);
    const auto second = audition::dsp::analyzeQuality(planar.view(), 0.5);
    EXPECT_DOUBLE_EQ(first.aggregate.rms_linear, second.aggregate.rms_linear);
    EXPECT_DOUBLE_EQ(first.aggregate.dc_offset, second.aggregate.dc_offset);
    EXPECT_DOUBLE_EQ(first.aggregate.clipping_ratio, second.aggregate.clipping_ratio);
    ASSERT_EQ(first.channels.size(), second.channels.size());
    for (std::size_t i = 0U; i < first.channels.size(); ++i) {
        EXPECT_DOUBLE_EQ(first.channels[i].rms_linear, second.channels[i].rms_linear);
        EXPECT_DOUBLE_EQ(first.channels[i].peak_linear, second.channels[i].peak_linear);
    }
}

TEST(AudioQuality, ClippingIncludesSamplesExactlyAtThreshold) {
    audition::AudioBuffer audio{{0.5F, -0.5F, 0.F, 1.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    EXPECT_DOUBLE_EQ(audition::dsp::signalMetrics(audio.view(), 0.5).clipping_ratio,
                     0.75);
    EXPECT_DOUBLE_EQ(audition::dsp::signalMetrics(audio.view(), 1.0).clipping_ratio,
                     0.25);
    EXPECT_DOUBLE_EQ(audition::dsp::signalMetrics(audio.view(), 0.0).clipping_ratio,
                     1.0);
}

TEST(AudioQuality, ZeroThresholdCountsSilentSamplesAsClippedByDefinition) {
    audition::AudioBuffer audio{{0.F, 0.F, 0.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto report = audition::dsp::analyzeQuality(audio.view(), -1.0);
    EXPECT_DOUBLE_EQ(report.aggregate.clipping_ratio, 1.0);
    EXPECT_DOUBLE_EQ(report.channels[0].clipping_ratio, 1.0);
    EXPECT_DOUBLE_EQ(report.aggregate.crest_factor_db, 0.0);
}

TEST(AudioQuality, AboveFullScaleSamplesRemainVisibleInMetrics) {
    audition::AudioBuffer audio{{-2.F, 1.5F, 0.5F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto metrics = audition::dsp::signalMetrics(audio.view(), 5.0);
    EXPECT_DOUBLE_EQ(metrics.peak_linear, 2.0);
    EXPECT_NEAR(metrics.peak_dbfs, 20.0 * std::log10(2.0), 1e-12);
    EXPECT_DOUBLE_EQ(metrics.clipping_ratio, 2.0 / 3.0);
}

TEST(AudioQuality, SnrReportsZeroAndNegativeDbForEqualAndNoisyLevels) {
    EXPECT_DOUBLE_EQ(audition::dsp::snrDb(0.5, 0.5), 0.0);
    EXPECT_NEAR(audition::dsp::snrDb(0.1, 1.0), -20.0, 1e-12);
    EXPECT_NEAR(audition::dsp::snrDb(1.0, 0.1), 20.0, 1e-12);
}
