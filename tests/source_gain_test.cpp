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

TEST(SourceGain, SpeechGainCanExceedLegacy24DbLimit) {
    auto gain = audition::dsp::makeSourceGain("rms");
    std::vector<float> quiet(160U, 0.0012f); // about -58.4 dBFS
    for (int i = 0; i < 400; ++i)
        (void)gain->process(quiet.data(), quiet.size(), 16000U);
    EXPECT_GT(gain->gainDb(), 24.0);
    EXPECT_LE(gain->gainDb(), 36.0);
}

TEST(SourceGain, ShortSilenceHoldsGainAndNoClipping) {
    auto gain = audition::dsp::makeSourceGain("rms");
    std::vector<float> speech(160U, 0.004f);
    for (int i = 0; i < 180; ++i)
        (void)gain->process(speech.data(), speech.size(), 16000U);
    const double before = gain->gainDb();
    std::vector<float> silence(160U, 0.0f);
    for (int i = 0; i < 12; ++i) {
        const auto output = gain->process(silence.data(), silence.size(), 16000U);
        EXPECT_EQ(output, silence);
    }
    EXPECT_NEAR(gain->gainDb(), before, 1.0);
    std::vector<float> transient(160U, 0.95f);
    const auto output = gain->process(transient.data(), transient.size(), 16000U);
    for (float sample : output) EXPECT_LE(std::abs(sample), 0.89f);
    gain->reset();
    EXPECT_DOUBLE_EQ(gain->gainDb(), 0.0);
}

#if defined(LIBAUDITION_WITH_SPEEXDSP_AGC) && LIBAUDITION_WITH_SPEEXDSP_AGC
TEST(SourceGain, SpeexHandlesUnevenInputFramesAndReset) {
    auto gain = audition::dsp::makeSourceGain("speex");
    std::vector<float> pcm(128U, 0.002F);
    for (int frame = 0; frame < 200; ++frame) {
        const auto result = gain->process(pcm.data(), pcm.size(), 16000U);
        ASSERT_EQ(result.size(), pcm.size());
        for (const float sample : result) {
            EXPECT_TRUE(std::isfinite(sample));
            EXPECT_LE(std::abs(sample), 1.0F);
        }
    }
    gain->reset();
    EXPECT_DOUBLE_EQ(gain->gainDb(), 0.0);
}
#endif

#if defined(LIBAUDITION_WITH_WEBRTC_APM) && LIBAUDITION_WITH_WEBRTC_APM
TEST(SourceGain, UnifiedApmAgc1AndAgc2HandleUnalignedHopsAndReset) {
    for (const char* implementation : {"webrtc_apm_agc1", "webrtc_apm_agc2"}) {
        auto gain = audition::dsp::makeSourceGain(implementation);
        std::vector<float> speech(128U, 0.003F);
        for (int hop = 0; hop < 250; ++hop) {
            const auto output = gain->process(speech.data(), speech.size(), 16000U);
            ASSERT_EQ(output.size(), speech.size());
            for (const float sample : output) {
                EXPECT_TRUE(std::isfinite(sample));
                EXPECT_LE(std::abs(sample), 1.0F);
            }
        }
        EXPECT_TRUE(std::isfinite(gain->gainDb()));
        EXPECT_THROW((void)gain->process(speech.data(), speech.size(), 48000U),
                     std::invalid_argument);
        gain->reset();
        EXPECT_DOUBLE_EQ(gain->gainDb(), 0.0);
        EXPECT_EQ(gain->process(speech.data(), speech.size(), 16000U).size(),
                  speech.size());
    }
}
#endif
