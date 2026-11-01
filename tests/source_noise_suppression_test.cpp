#include <audition/dsp/source_noise_suppressor.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

TEST(SourceNoiseSuppression, BypassPreservesSamplesAndRejectsInvalidInput) {
    auto bypass = audition::dsp::makeBypassSourceNoiseSuppressor();
    const std::vector<float> pcm{0.0F, 0.003F, -0.5F, 1.0F, -1.0F};
    EXPECT_EQ(bypass->process(pcm.data(), pcm.size(), 16000U), pcm);
    EXPECT_TRUE(bypass->process(nullptr, 0U, 16000U).empty());
    EXPECT_THROW((void)bypass->process(nullptr, 1U, 16000U),
                 std::invalid_argument);
    EXPECT_THROW((void)bypass->process(pcm.data(), pcm.size(), 0U),
                 std::invalid_argument);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW((void)bypass->process(&nan, 1U, 16000U),
                 std::invalid_argument);
    bypass->reset();
    EXPECT_EQ(bypass->process(pcm.data(), pcm.size(), 16000U), pcm);
}

#if defined(LIBAUDITION_WITH_WEBRTC_APM) && LIBAUDITION_WITH_WEBRTC_APM
#include <audition/backends/webrtc_apm_noise_suppressor.hpp>

namespace {
std::vector<float> makeTestPcm(std::size_t size) {
    std::vector<float> pcm(size);
    for (std::size_t i = 0; i < size; ++i)
        pcm[i] = static_cast<float>(static_cast<int>(i % 97U) - 48) / 1000.0F;
    return pcm;
}
} // namespace

TEST(WebRtcApmNoiseSuppressor, LevelsRatesAndReset) {
    using audition::dsp::NoiseSuppressionLevel;
    for (auto level : {NoiseSuppressionLevel::low,
                       NoiseSuppressionLevel::moderate,
                       NoiseSuppressionLevel::high,
                       NoiseSuppressionLevel::very_high}) {
        for (auto rate : {16000U, 32000U, 48000U}) {
            auto ns = audition::dsp::makeWebRtcApmSourceNoiseSuppressor(level);
            const auto pcm = makeTestPcm(static_cast<std::size_t>(rate / 100U) * 13U);
            const auto output = ns->process(pcm.data(), pcm.size(), rate);
            ASSERT_EQ(output.size(), pcm.size());
            for (float sample : output) {
                EXPECT_TRUE(std::isfinite(sample));
                EXPECT_LE(std::abs(sample), 1.0F);
            }
            EXPECT_TRUE(ns->process(nullptr, 0U, rate).empty());
            const auto other_rate = rate == 16000U ? 48000U : 16000U;
            EXPECT_THROW((void)ns->process(pcm.data(), pcm.size(), other_rate),
                         std::invalid_argument);
            ns->reset();
            EXPECT_EQ(ns->process(pcm.data(), pcm.size(), rate), output);
        }
    }
    EXPECT_THROW((void)audition::dsp::makeWebRtcApmSourceNoiseSuppressor(
                     static_cast<NoiseSuppressionLevel>(255)),
                 std::invalid_argument);
}

TEST(WebRtcApmNoiseSuppressor, ChunkBoundariesAndSourcePcmIsolation) {
    for (auto rate : {16000U, 32000U, 48000U}) {
        const auto pcm = makeTestPcm(static_cast<std::size_t>(rate / 100U) * 13U + 47U);
        auto whole = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
        auto split = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
        const auto expected = whole->process(pcm.data(), pcm.size(), rate);
        std::vector<float> actual;
        actual.reserve(pcm.size());
        const std::size_t hops[] = {1U, 41U, 3U, 127U, 480U, 13U};
        std::size_t index = 0U, hop = 0U;
        while (index < pcm.size()) {
            const auto count = std::min(hops[hop % 6U], pcm.size() - index);
            const auto out = split->process(pcm.data() + index, count, rate);
            actual.insert(actual.end(), out.begin(), out.end());
            index += count;
            ++hop;
        }
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(pcm, makeTestPcm(pcm.size()));
    }
}

TEST(WebRtcApmNoiseSuppressor, NonfiniteOrUnnormalizedChunkIsAtomic) {
    auto actual = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
    auto expected = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
    const auto prefix = makeTestPcm(64U);
    EXPECT_EQ(actual->process(prefix.data(), prefix.size(), 16000U),
              expected->process(prefix.data(), prefix.size(), 16000U));
    auto chunk = makeTestPcm(96U);
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(), 1.25F, -1.25F}) {
        chunk[48U] = bad;
        EXPECT_THROW((void)actual->process(chunk.data(), chunk.size(), 16000U),
                     std::invalid_argument);
    }
    EXPECT_THROW((void)actual->process(nullptr, chunk.size(), 16000U),
                 std::invalid_argument);
    const auto recovery = makeTestPcm(160U);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(actual->process(recovery.data(), recovery.size(), 16000U),
                  expected->process(recovery.data(), recovery.size(), 16000U));
    actual->reset();
    EXPECT_EQ(actual->process(recovery.data(), recovery.size(), 16000U),
              audition::dsp::makeWebRtcApmSourceNoiseSuppressor()->process(
                  recovery.data(), recovery.size(), 16000U));
}
#endif
