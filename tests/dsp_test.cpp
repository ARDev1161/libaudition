#include <acoustic/dsp/basic.hpp>

#include <gtest/gtest.h>

TEST(Dsp, MixesStereoToMono) {
    acoustic::AudioBuffer audio{{1.F, 3.F, -1.F, 1.F},
                                {16000, 2, acoustic::AudioLayout::Interleaved},
                                acoustic::Timestamp{}};
    const auto mono = acoustic::dsp::mixToMono(audio.view());
    ASSERT_EQ(mono.samples().size(), 2U);
    EXPECT_FLOAT_EQ(mono.samples()[0], 2.F);
    EXPECT_FLOAT_EQ(mono.samples()[1], 0.F);
}

TEST(Dsp, ConvertsLayoutWithoutChangingChannels) {
    acoustic::AudioBuffer audio{{1.F, 10.F, 2.F, 20.F},
                                {16000, 2, acoustic::AudioLayout::Interleaved},
                                acoustic::Timestamp{}};
    const auto planar = acoustic::dsp::convertLayout(audio.view(), acoustic::AudioLayout::Planar);
    EXPECT_FLOAT_EQ(planar.samples()[0], 1.F);
    EXPECT_FLOAT_EQ(planar.samples()[1], 2.F);
    EXPECT_FLOAT_EQ(planar.samples()[2], 10.F);
    EXPECT_FLOAT_EQ(planar.samples()[3], 20.F);
}
