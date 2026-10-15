#include <audition/dsp/basic.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

TEST(Dsp, MixesStereoToMono) {
    audition::AudioBuffer audio{{1.F, 3.F, -1.F, 1.F},
                                {16000, 2, audition::AudioLayout::Interleaved},
                                audition::Timestamp{}};
    const auto mono = audition::dsp::mixToMono(audio.view());
    ASSERT_EQ(mono.samples().size(), 2U);
    EXPECT_FLOAT_EQ(mono.samples()[0], 2.F);
    EXPECT_FLOAT_EQ(mono.samples()[1], 0.F);
}

TEST(Dsp, ConvertsLayoutWithoutChangingChannels) {
    audition::AudioBuffer audio{{1.F, 10.F, 2.F, 20.F},
                                {16000, 2, audition::AudioLayout::Interleaved},
                                audition::Timestamp{}};
    const auto planar = audition::dsp::convertLayout(audio.view(), audition::AudioLayout::Planar);
    EXPECT_FLOAT_EQ(planar.samples()[0], 1.F);
    EXPECT_FLOAT_EQ(planar.samples()[1], 2.F);
    EXPECT_FLOAT_EQ(planar.samples()[2], 10.F);
    EXPECT_FLOAT_EQ(planar.samples()[3], 20.F);
}


TEST(Dsp, MeasuresRmsPeakAndClippingAcrossAllSamples) {
    audition::AudioBuffer audio{{-1.F, 0.F, 0.5F, -0.5F},
                                {16000, 2, audition::AudioLayout::Interleaved},
                                audition::Timestamp{}};
    EXPECT_NEAR(audition::dsp::rms(audio.view()), std::sqrt(0.375), 1e-7);
    EXPECT_NEAR(audition::dsp::channelRms(audio.view(), 0), std::sqrt(0.625), 1e-7);
    EXPECT_NEAR(audition::dsp::channelRms(audio.view(), 1), std::sqrt(0.125), 1e-7);
    EXPECT_DOUBLE_EQ(audition::dsp::peakAbsolute(audio.view()), 1.0);
    EXPECT_DOUBLE_EQ(audition::dsp::clippingRatio(audio.view(), 0.5), 0.75);
    EXPECT_DOUBLE_EQ(audition::dsp::clippingRatio(audio.view(), 2.0), 0.25);
    EXPECT_DOUBLE_EQ(audition::dsp::clippingRatio(audio.view(), -1.0), 1.0);
    EXPECT_THROW(audition::dsp::channelRms(audio.view(), 2), audition::Error);
}

TEST(Dsp, EmptyAudioHasZeroLevelsAndPreservesMetadataOnConversion) {
    const audition::Timestamp capture{123456, {audition::ClockDomain::Simulation, 7}};
    audition::AudioBuffer audio{{},
                                {16000, 2, audition::AudioLayout::Interleaved},
                                capture, 42};
    EXPECT_DOUBLE_EQ(audition::dsp::rms(audio.view()), 0.0);
    EXPECT_DOUBLE_EQ(audition::dsp::channelRms(audio.view(), 1), 0.0);
    EXPECT_DOUBLE_EQ(audition::dsp::peakAbsolute(audio.view()), 0.0);
    EXPECT_DOUBLE_EQ(audition::dsp::clippingRatio(audio.view()), 0.0);
    const auto planar = audition::dsp::convertLayout(audio.view(), audition::AudioLayout::Planar);
    EXPECT_TRUE(planar.samples().empty());
    EXPECT_EQ(planar.sequenceNumber(), 42U);
    EXPECT_EQ(planar.captureTime().nanoseconds(), capture.nanoseconds());
    const auto mono = audition::dsp::mixToMono(audio.view());
    EXPECT_TRUE(mono.samples().empty());
    EXPECT_EQ(mono.format().channel_count, 1U);
    EXPECT_EQ(mono.sequenceNumber(), 42U);
}

TEST(Dsp, RoundTripsThreeChannelLayoutAndPreservesCaptureIdentity) {
    const audition::Timestamp capture{98765, {audition::ClockDomain::Simulation, 3}};
    audition::AudioBuffer audio{{1.F, 10.F, 100.F, 2.F, 20.F, 200.F},
                                {48000, 3, audition::AudioLayout::Interleaved},
                                capture, 91};
    const auto planar = audition::dsp::convertLayout(audio.view(), audition::AudioLayout::Planar);
    EXPECT_EQ(planar.samples(), (std::vector<float>{1.F, 2.F, 10.F, 20.F, 100.F, 200.F}));
    const auto roundtrip = audition::dsp::convertLayout(planar.view(),
                                                        audition::AudioLayout::Interleaved);
    EXPECT_EQ(roundtrip.samples(), audio.samples());
    const auto identical = audition::dsp::convertLayout(audio.view(),
                                                        audition::AudioLayout::Interleaved);
    EXPECT_EQ(identical.samples(), audio.samples());
    EXPECT_EQ(roundtrip.captureTime().clock(), capture.clock());
    EXPECT_EQ(roundtrip.captureTime().nanoseconds(), capture.nanoseconds());
    EXPECT_EQ(roundtrip.sequenceNumber(), 91U);
    EXPECT_EQ(roundtrip.format().sample_rate_hz, 48000U);
}

TEST(Dsp, MixesPlanarThreeChannelAudioAndKeepsTiming) {
    audition::AudioBuffer audio{{3.F, 6.F, 6.F, 9.F, -3.F, -6.F},
                                {16000, 3, audition::AudioLayout::Planar},
                                audition::Timestamp{}, 9};
    const auto mono = audition::dsp::mixToMono(audio.view());
    ASSERT_EQ(mono.samples().size(), 2U);
    EXPECT_FLOAT_EQ(mono.samples()[0], 2.F);
    EXPECT_FLOAT_EQ(mono.samples()[1], 3.F);
    EXPECT_EQ(mono.format().layout, audition::AudioLayout::Interleaved);
    EXPECT_EQ(mono.sequenceNumber(), 9U);
}
