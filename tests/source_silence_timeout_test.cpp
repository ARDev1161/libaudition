#include "../demo/qt/source_silence_timeout.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <vector>

TEST(SourceSilenceTimeout, ExactlyThreeSecondsOfSourceLocalSilence) {
    demo::SourceSilenceTimeout gate{16000U};
    const std::vector<float> loud(160U, 0.01F);
    const std::vector<float> silent(160U, 0.0F);
    EXPECT_TRUE(gate.observe(1U, loud.data(), loud.size()));
    for (int i = 0; i < 299; ++i)
        EXPECT_TRUE(gate.observe(1U, silent.data(), silent.size()));
    EXPECT_FALSE(gate.observe(1U, silent.data(), silent.size()));
    EXPECT_TRUE(gate.observe(2U, loud.data(), loud.size()));
    // Below the 6 dB re-entry hysteresis, a retired ID stays dormant.
    const std::vector<float> marginal(160U, 0.002F);
    EXPECT_FALSE(gate.observe(1U, marginal.data(), marginal.size()));
    EXPECT_TRUE(gate.observe(1U, loud.data(), loud.size()));
}

TEST(SourceSilenceTimeout, MissingAudioAndIdReuse) {
    demo::SourceSilenceTimeout gate{1000U};
    EXPECT_TRUE(gate.observeMissing(8U, 1000U));
    EXPECT_TRUE(gate.observeMissing(8U, 1000U));
    EXPECT_FALSE(gate.observeMissing(8U, 1000U));
    gate.retain({9U});
    EXPECT_TRUE(gate.observeMissing(8U, 1000U));
    gate.reset();
    EXPECT_TRUE(gate.observeMissing(8U, 1000U));
}

TEST(SourceSilenceTimeout, UninterruptedSpeechDoesNotBecomeNoiseFloor) {
    demo::SourceSilenceTimeout gate{1000U};
    const std::vector<float> voice(1000U, 0.01F);
    const std::vector<float> quiet(1000U, 0.0001F);
    for (int i = 0; i < 12; ++i)
        EXPECT_TRUE(gate.observe(1U, voice.data(), voice.size()));
    EXPECT_TRUE(gate.observe(1U, quiet.data(), quiet.size()));
    EXPECT_TRUE(gate.observe(1U, voice.data(), voice.size()));
    EXPECT_TRUE(gate.observe(1U, quiet.data(), quiet.size()));
    EXPECT_TRUE(gate.observe(1U, quiet.data(), quiet.size()));
    EXPECT_FALSE(gate.observe(1U, quiet.data(), quiet.size()));
}

TEST(SourceSilenceTimeout, ConfigValidationAndDisable) {
    demo::SourceSilenceTimeout disabled{16000U, 0.0};
    EXPECT_TRUE(disabled.observeMissing(7U, 160000U));
    EXPECT_THROW(demo::SourceSilenceTimeout(0U), std::invalid_argument);
    EXPECT_THROW(demo::SourceSilenceTimeout(16000U, -1.0), std::invalid_argument);
    EXPECT_THROW(demo::SourceSilenceTimeout(16000U, 3.0, 5.0), std::invalid_argument);
    demo::SourceSilenceTimeout gate{16000U};
    EXPECT_THROW((void)gate.observe(1U, nullptr, 2U), std::invalid_argument);
    const float bad[] = {0.2F, std::numeric_limits<float>::quiet_NaN()};
    EXPECT_THROW((void)gate.observe(1U, bad, 2U), std::invalid_argument);
}
