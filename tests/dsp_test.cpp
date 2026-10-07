#include <audition/dsp/basic.hpp>

#include <gtest/gtest.h>

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
