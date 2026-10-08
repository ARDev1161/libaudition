#include <audition/dsp/quality.hpp>

#include <cmath>

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
