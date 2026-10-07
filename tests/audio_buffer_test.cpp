#include <acoustic/audio/audio_buffer.hpp>

#include <gtest/gtest.h>

TEST(AudioBuffer, InterleavedChannelViewIsStrided) {
    acoustic::AudioBuffer buffer{{1.F, 10.F, 2.F, 20.F},
                                 {16000, 2, acoustic::AudioLayout::Interleaved},
                                 acoustic::Timestamp{}};
    const auto left = buffer.channel(0);
    const auto right = buffer.channel(1);
    ASSERT_EQ(left.size(), 2U);
    EXPECT_FLOAT_EQ(left[0], 1.F);
    EXPECT_FLOAT_EQ(left[1], 2.F);
    EXPECT_FLOAT_EQ(right[0], 10.F);
    EXPECT_FLOAT_EQ(right[1], 20.F);
}

TEST(AudioBuffer, PlanarChannelViewIsContiguous) {
    acoustic::AudioBuffer buffer{{1.F, 2.F, 10.F, 20.F},
                                 {16000, 2, acoustic::AudioLayout::Planar},
                                 acoustic::Timestamp{}};
    EXPECT_EQ(buffer.channel(1).stride(), 1);
    EXPECT_FLOAT_EQ(buffer.channel(1)[0], 10.F);
}

TEST(AudioBuffer, RejectsPartialFrame) {
    EXPECT_THROW((acoustic::AudioBuffer{{1.F, 2.F, 3.F},
                                        {16000, 2, acoustic::AudioLayout::Interleaved},
                                        acoustic::Timestamp{}}),
                 acoustic::Error);
}
