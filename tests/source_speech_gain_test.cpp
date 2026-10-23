#include "../demo/qt/source_speech_gain.hpp"
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
std::vector<float> sine(std::size_t n, float amplitude) {
    std::vector<float> data(n);
    for (std::size_t i = 0; i < n; ++i)
        data[i] = amplitude * static_cast<float>(
            std::sin(2.0 * 3.141592653589793 * static_cast<double>(i) / 32.0));
    return data;
}
}

TEST(SourceSpeechGain, AmplifiesQuietSpeechWithoutClipping) {
    demo::SourceSpeechGain gain;
    const auto samples = sine(128U, 0.004F);
    std::vector<float> processed;
    for (int i = 0; i < 90; ++i)
        processed = gain.process(samples.data(), samples.size(), 16000U);
    EXPECT_GT(gain.metrics().applied_gain_db, 15.0);
    EXPECT_GT(gain.metrics().output_rms_dbfs -
              gain.metrics().input_rms_dbfs, 15.0);
    for (const float sample : processed) {
        EXPECT_TRUE(std::isfinite(sample));
        EXPECT_LE(std::abs(sample), 0.890001F);
    }
}

TEST(SourceSpeechGain, DoesNotPumpDigitalSilenceOrSubthresholdNoise) {
    demo::SourceSpeechGain gain;
    const std::vector<float> zeros(128U, 0.0F);
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(gain.process(zeros.data(), zeros.size(), 16000U), zeros);
    const auto noise = sine(128U, 0.0001F);
    for (int i = 0; i < 50; ++i)
        static_cast<void>(gain.process(noise.data(), noise.size(), 16000U));
    EXPECT_NEAR(gain.metrics().applied_gain_db, 0.0, 1.e-9);
}

TEST(SourceSpeechGain, ProtectsSuddenLoudOnset) {
    demo::SourceSpeechGain gain;
    const auto quiet = sine(128U, 0.004F);
    for (int i = 0; i < 90; ++i)
        static_cast<void>(gain.process(quiet.data(), quiet.size(), 16000U));
    const auto loud = sine(128U, 0.95F);
    const auto processed = gain.process(loud.data(), loud.size(), 16000U);
    for (const float value : processed)
        EXPECT_LE(std::abs(value), 0.890001F);
    EXPECT_LE(gain.metrics().applied_gain_db, 0.0);
}

TEST(SourceSpeechGain, EachTrackOwnsItsGainAndCanReset) {
    demo::SourceSpeechGain near, far;
    const auto loud = sine(128U, 0.4F);
    const auto quiet = sine(128U, 0.004F);
    for (int i = 0; i < 90; ++i) {
        static_cast<void>(near.process(loud.data(), loud.size(), 16000U));
        static_cast<void>(far.process(quiet.data(), quiet.size(), 16000U));
    }
    EXPECT_NEAR(near.metrics().applied_gain_db, 0.0, 1.e-6);
    EXPECT_GT(far.metrics().applied_gain_db, 15.0);
    far.reset();
    EXPECT_EQ(far.metrics().applied_gain_db, 0.0);
    EXPECT_EQ(far.metrics().input_rms_dbfs, -180.0);
}

TEST(SourceSpeechGain, InvalidSamplesAreRejectedWithoutChangingGain) {
    demo::SourceSpeechGain gain;
    EXPECT_THROW(static_cast<void>(gain.process(nullptr, 128U, 16000U)), std::invalid_argument);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(gain.process(&nan, 1U, 16000U)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(gain.process(&nan, 1U, 0U)), std::invalid_argument);
    EXPECT_EQ(gain.metrics().applied_gain_db, 0.0);
}
