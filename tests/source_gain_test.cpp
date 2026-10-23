#include <audition/dsp/source_gain.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(SourceGain, FactoryAndValidation) {
    auto gain = audition::dsp::makeSourceGain("rms");
    ASSERT_NE(gain, nullptr);
    EXPECT_DOUBLE_EQ(gain->gainDb(), 0.0);
    EXPECT_THROW((void)audition::dsp::makeSourceGain("unknown"), std::invalid_argument);
    EXPECT_THROW((void)gain->process(nullptr, 128U, 16000U), std::invalid_argument);
    EXPECT_THROW((void)gain->process(nullptr, 0U, 0U), std::invalid_argument);
}

TEST(SourceGain, QuietSpeechIsAmplifiedWithoutModifyingInput) {
    auto gain = audition::dsp::makeSourceGain("rms");
    std::vector<float> input(160U, 0.008f);
    for (int i = 0; i < 100; ++i) {
        auto output = gain->process(input.data(), input.size(), 16000U);
        ASSERT_EQ(output.size(), input.size());
        EXPECT_TRUE(std::isfinite(output.front()));
        EXPECT_LE(std::abs(output.front()), 0.89f);
    }
    EXPECT_GT(gain->gainDb(), 0.0);
    EXPECT_FLOAT_EQ(input.front(), 0.008f);
    gain->reset();
    EXPECT_DOUBLE_EQ(gain->gainDb(), 0.0);
}

TEST(SourceGain, SilenceDoesNotAmplifyNoise) {
    auto gain = audition::dsp::makeSourceGain("rms");
    std::vector<float> silence(160U, 0.0f);
    const auto output = gain->process(silence.data(), silence.size(), 16000U);
    EXPECT_EQ(output, silence);
    EXPECT_DOUBLE_EQ(gain->gainDb(), 0.0);
}
