#include <audition/dsp/source_gain.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
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

// Failed input must not advance the buffered partial 10-ms frame.
TEST(SourceGain, SpeexRejectsNonfiniteChunkWithoutMutatingStream) {
    auto subject = audition::dsp::makeSourceGain("speex");
    auto control = audition::dsp::makeSourceGain("speex");
    const std::vector<float> prefix(64U, 0.002F);
    EXPECT_EQ(subject->process(prefix.data(), prefix.size(), 16000U),
              control->process(prefix.data(), prefix.size(), 16000U));

    std::vector<float> invalid(96U, 0.003F);
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()}) {
        invalid[48U] = bad;
        EXPECT_THROW((void)subject->process(invalid.data(), invalid.size(), 16000U),
                     std::invalid_argument);
    }

    const std::vector<float> recovery(160U, 0.004F);
    for (int frame = 0; frame < 4; ++frame) {
        EXPECT_EQ(subject->process(recovery.data(), recovery.size(), 16000U),
                  control->process(recovery.data(), recovery.size(), 16000U));
        EXPECT_DOUBLE_EQ(subject->gainDb(), control->gainDb());
    }
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

// A rejected chunk must not leak a valid prefix into the next 10-ms APM
// frame. Compare against an independent engine that never received it.
TEST(SourceGain, UnifiedApmRejectsNonfiniteChunkWithoutMutatingStream) {
    for (const char* implementation : {"webrtc_apm_agc1", "webrtc_apm_agc2"}) {
        auto subject = audition::dsp::makeSourceGain(implementation);
        auto control = audition::dsp::makeSourceGain(implementation);
        const std::vector<float> prefix(64U, 0.002F);
        EXPECT_EQ(subject->process(prefix.data(), prefix.size(), 16000U),
                  control->process(prefix.data(), prefix.size(), 16000U));

        std::vector<float> invalid(96U, 0.003F);
        for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()}) {
            invalid[48U] = bad;
            EXPECT_THROW((void)subject->process(
                             invalid.data(), invalid.size(), 16000U),
                         std::invalid_argument);
        }

        const std::vector<float> recovery(160U, 0.004F);
        for (int frame = 0; frame < 4; ++frame) {
            EXPECT_EQ(subject->process(recovery.data(), recovery.size(), 16000U),
                      control->process(recovery.data(), recovery.size(), 16000U));
            EXPECT_DOUBLE_EQ(subject->gainDb(), control->gainDb());
        }
    }
}

// The initial 10-ms algorithmic buffering and all later samples must be
// identical regardless of where the caller splits the source PCM.
TEST(SourceGain, UnifiedApmOutputIsInvariantToInputChunkBoundaries) {
    for (const char* implementation : {"webrtc_apm_agc1", "webrtc_apm_agc2"}) {
        for (const auto rate : {16000U, 32000U, 48000U}) {
            const auto frames = static_cast<std::size_t>(rate / 100U);
            std::vector<float> pcm(frames * 13U);
            for (std::size_t i = 0; i < pcm.size(); ++i)
                pcm[i] = static_cast<float>(
                    static_cast<int>(i % 71U) - 35) / 2000.0F;

            auto contiguous = audition::dsp::makeSourceGain(implementation);
            auto fragmented = audition::dsp::makeSourceGain(implementation);
            const auto reference = contiguous->process(pcm.data(), pcm.size(), rate);

            std::vector<float> streamed;
            streamed.reserve(pcm.size());
            constexpr std::size_t hops[] = {1U, 31U, 223U, 7U, 480U, 2U, 101U};
            std::size_t pos = 0U;
            std::size_t hop_index = 0U;
            while (pos < pcm.size()) {
                const auto count = std::min(hops[hop_index % 7U],
                                            pcm.size() - pos);
                const auto part = fragmented->process(pcm.data() + pos, count, rate);
                streamed.insert(streamed.end(), part.begin(), part.end());
                pos += count;
                ++hop_index;
            }
            EXPECT_EQ(streamed, reference);
            EXPECT_DOUBLE_EQ(fragmented->gainDb(), contiguous->gainDb());

            fragmented->reset();
            EXPECT_DOUBLE_EQ(fragmented->gainDb(), 0.0);
            EXPECT_EQ(fragmented->process(pcm.data(), pcm.size(), rate), reference);
        }
    }
}
#endif
