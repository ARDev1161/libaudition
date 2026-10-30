#include <audition/dsp/source_speech_chain.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct NsSpy final : audition::dsp::ISourceNoiseSuppressor {
    std::vector<int>* order{};
    bool truncate{false}, nonfinite{false};
    std::vector<float> process(const float* in, std::size_t n, std::uint32_t) override {
        order->push_back(1);
        std::vector<float> out(in, in + n);
        for (auto& s : out) s *= 0.5F;
        if (truncate) out.pop_back();
        if (nonfinite) out.front() = std::numeric_limits<float>::quiet_NaN();
        return out;
    }
    void reset() noexcept override {}
};
struct GainSpy final : audition::dsp::ISourceGain {
    std::vector<int>* order{};
    bool nonfinite{false};
    std::vector<float> process(const float* in, std::size_t n, std::uint32_t) override {
        order->push_back(2);
        std::vector<float> out(in, in + n);
        for (auto& s : out) s *= 3.0F;
        if (nonfinite) out.front() = std::numeric_limits<float>::infinity();
        return out;
    }
    double gainDb() const noexcept override { return 0.0; }
    void reset() noexcept override {}
};
} // namespace

TEST(SourceSpeechChain, RunsNSBeforeAGCWithoutMutatingInput) {
    std::vector<int> order;
    NsSpy ns; ns.order = &order;
    GainSpy gain; gain.order = &order;
    const std::vector<float> pcm{0.2F, -0.4F, 0.6F, 0.0F};
    const auto output = audition::dsp::processSourceSpeechChain(
        pcm.data(), pcm.size(), 16000U, &ns, &gain);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    ASSERT_EQ(output.size(), pcm.size());
    for (std::size_t i = 0; i < pcm.size(); ++i)
        EXPECT_FLOAT_EQ(output[i], pcm[i] * 0.5F * 3.0F);
    EXPECT_EQ(pcm, (std::vector<float>{0.2F, -0.4F, 0.6F, 0.0F}));

    order.clear();
    EXPECT_EQ(audition::dsp::processSourceSpeechChain(
                  pcm.data(), pcm.size(), 16000U, nullptr, nullptr), pcm);
    EXPECT_TRUE(order.empty());
    const auto no_gain = audition::dsp::processSourceSpeechChain(
        pcm.data(), pcm.size(), 16000U, &ns, nullptr);
    EXPECT_EQ(order, (std::vector<int>{1}));
    for (std::size_t i = 0; i < pcm.size(); ++i)
        EXPECT_FLOAT_EQ(no_gain[i], pcm[i] * 0.5F);
}

TEST(SourceSpeechChain, RejectsInvalidInputsAndBackendResults) {
    std::vector<int> order;
    NsSpy ns; ns.order = &order;
    GainSpy gain; gain.order = &order;
    const std::vector<float> pcm(32U, 0.1F);
    EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
        nullptr, pcm.size(), 16000U, &ns, &gain), std::invalid_argument);
    EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
        pcm.data(), pcm.size(), 0U, &ns, &gain), std::invalid_argument);
    const float bad = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
        &bad, 1U, 16000U, &ns, &gain), std::invalid_argument);
    EXPECT_TRUE(order.empty());
    ns.truncate = true;
    EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
        pcm.data(), pcm.size(), 16000U, &ns, &gain), std::runtime_error);
    EXPECT_EQ(order, (std::vector<int>{1}));
    ns.truncate = false; ns.nonfinite = true;
    EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
        pcm.data(), pcm.size(), 16000U, &ns, &gain), std::runtime_error);
    EXPECT_EQ(order.back(), 1);
    ns.nonfinite = false; gain.nonfinite = true;
    EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
        pcm.data(), pcm.size(), 16000U, &ns, &gain), std::runtime_error);
    EXPECT_TRUE(audition::dsp::processSourceSpeechChain(
        nullptr, 0U, 16000U, &ns, &gain).empty());
}

#if defined(LIBAUDITION_WITH_WEBRTC_APM) && LIBAUDITION_WITH_WEBRTC_APM
#include <audition/backends/webrtc_apm_noise_suppressor.hpp>
TEST(SourceSpeechChain, WebRtcNSAGC2IndependentSourcesAndChunkBoundaries) {
    constexpr std::uint32_t rate = 16000U;
    std::vector<float> first(160U * 15U + 47U), second(first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        first[i] = static_cast<float>(static_cast<int>(i % 79U) - 39) / 2000.0F;
        second[i] = static_cast<float>(static_cast<int>(i % 103U) - 51) / 3000.0F;
    }
    auto nsa = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
    auto nsb = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
    auto ga = audition::dsp::makeSourceGain("webrtc_apm_agc2");
    auto gb = audition::dsp::makeSourceGain("webrtc_apm_agc2");
    const auto expectedA = audition::dsp::processSourceSpeechChain(
        first.data(), first.size(), rate, nsa.get(), ga.get());
    const auto expectedB = audition::dsp::processSourceSpeechChain(
        second.data(), second.size(), rate, nsb.get(), gb.get());
    auto fragmentedNsA = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
    auto fragmentedNsB = audition::dsp::makeWebRtcApmSourceNoiseSuppressor();
    auto fragmentedGa = audition::dsp::makeSourceGain("webrtc_apm_agc2");
    auto fragmentedGb = audition::dsp::makeSourceGain("webrtc_apm_agc2");
    std::vector<float> actualA, actualB;
    constexpr std::size_t hops[] = {1U, 31U, 223U, 7U, 480U, 2U, 101U};
    std::size_t offset = 0U;
    for (std::size_t i = 0U; offset < first.size(); ++i) {
        const auto count = std::min(hops[i % 7U], first.size() - offset);
        auto outA = audition::dsp::processSourceSpeechChain(
            first.data() + offset, count, rate, fragmentedNsA.get(), fragmentedGa.get());
        auto outB = audition::dsp::processSourceSpeechChain(
            second.data() + offset, count, rate, fragmentedNsB.get(), fragmentedGb.get());
        actualA.insert(actualA.end(), outA.begin(), outA.end());
        actualB.insert(actualB.end(), outB.begin(), outB.end());
        offset += count;
    }
    EXPECT_EQ(actualA, expectedA);
    EXPECT_EQ(actualB, expectedB);
    EXPECT_NE(actualA, actualB);
    EXPECT_DOUBLE_EQ(ga->gainDb(), fragmentedGa->gainDb());
    EXPECT_DOUBLE_EQ(gb->gainDb(), fragmentedGb->gainDb());
    fragmentedNsA->reset();
    fragmentedGa->reset();
    EXPECT_EQ(audition::dsp::processSourceSpeechChain(
        first.data(), first.size(), rate, fragmentedNsA.get(), fragmentedGa.get()),
        expectedA);
}
#endif
