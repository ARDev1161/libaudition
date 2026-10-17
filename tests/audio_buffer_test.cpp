#include <audition/audio/audio_buffer.hpp>

#include <gtest/gtest.h>

TEST(AudioBuffer, InterleavedChannelViewIsStrided) {
    audition::AudioBuffer buffer{{1.F, 10.F, 2.F, 20.F},
                                 {16000, 2, audition::AudioLayout::Interleaved},
                                 audition::Timestamp{}};
    const auto left = buffer.channel(0);
    const auto right = buffer.channel(1);
    ASSERT_EQ(left.size(), 2U);
    EXPECT_FLOAT_EQ(left[0], 1.F);
    EXPECT_FLOAT_EQ(left[1], 2.F);
    EXPECT_FLOAT_EQ(right[0], 10.F);
    EXPECT_FLOAT_EQ(right[1], 20.F);
}

TEST(AudioBuffer, PlanarChannelViewIsContiguous) {
    audition::AudioBuffer buffer{{1.F, 2.F, 10.F, 20.F},
                                 {16000, 2, audition::AudioLayout::Planar},
                                 audition::Timestamp{}};
    EXPECT_EQ(buffer.channel(1).stride(), 1);
    EXPECT_FLOAT_EQ(buffer.channel(1)[0], 10.F);
}

TEST(AudioBuffer, RejectsPartialFrame) {
    EXPECT_THROW((audition::AudioBuffer{{1.F, 2.F, 3.F},
                                        {16000, 2, audition::AudioLayout::Interleaved},
                                        audition::Timestamp{}}),
                 audition::Error);
}


TEST(AudioBuffer, RejectsInvalidFormatsAndNullNonemptyAudioViews) {
    const audition::Timestamp time{};
    EXPECT_THROW((audition::AudioBuffer{{1.F},
        {0U, 1U, audition::AudioLayout::Interleaved}, time}), audition::Error);
    EXPECT_THROW((audition::AudioBuffer{{1.F},
        {16000U, 0U, audition::AudioLayout::Interleaved}, time}), audition::Error);
    EXPECT_THROW((audition::AudioView{nullptr, 1U,
        {16000U, 1U, audition::AudioLayout::Interleaved}, time}), audition::Error);
    EXPECT_THROW((audition::AudioView{nullptr, 0U,
        {0U, 1U, audition::AudioLayout::Interleaved}, time}), audition::Error);
}

TEST(AudioBuffer, EmptyViewAndEmptyValidBufferPreserveMetadata) {
    audition::AudioView uninitialized{};
    EXPECT_EQ(uninitialized.frameCount(), 0U);
    EXPECT_EQ(uninitialized.duration().nanoseconds(), 0);
    const audition::Timestamp captured{987,
        {audition::ClockDomain::Simulation, 3U}};
    audition::AudioBuffer buffer{{},
        {16000U, 2U, audition::AudioLayout::Planar}, captured, 44U};
    const auto view = buffer.view();
    EXPECT_EQ(view.frameCount(), 0U);
    EXPECT_EQ(view.duration().nanoseconds(), 0);
    EXPECT_EQ(view.sequenceNumber(), 44U);
    EXPECT_EQ(view.captureTime().nanoseconds(), 987);
    EXPECT_EQ(view.channel(0).size(), 0U);
    EXPECT_EQ(view.channel(1).size(), 0U);
    EXPECT_THROW(static_cast<void>(view.channel(2)), audition::Error);
}

TEST(AudioBuffer, DurationAndMutableSamplesTrackFramesNotChannels) {
    audition::AudioBuffer buffer{{0.F, 1.F, 2.F, 3.F},
        {16000U, 2U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}, 7U};
    EXPECT_EQ(buffer.frameCount(), 2U);
    EXPECT_EQ(buffer.duration().nanoseconds(), 125000);
    auto samples = buffer.mutableSamples();
    samples[0] = -1.0F;
    EXPECT_FLOAT_EQ(buffer.view().channel(0)[0], -1.0F);
    EXPECT_EQ(buffer.view().sequenceNumber(), 7U);
    EXPECT_THROW(static_cast<void>(buffer.channel(2)), audition::Error);
}
