#include <audition/dsp/source_speech_chain.hpp>
#include <audition/dsp/source_speech_failure_tracker.hpp>
#include <audition/dsp/streaming_level_meter.hpp>
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
    bool truncate{false}, nonfinite{false}, out_of_range{false};
    std::vector<float> process(const float* in, std::size_t n, std::uint32_t) override {
        order->push_back(1);
        std::vector<float> out(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = in[i] * 0.5F;
        if (truncate) out.pop_back();
        if (nonfinite) out.front() = std::numeric_limits<float>::quiet_NaN();
        if (out_of_range) out.front() = 1.25F;
        return out;
    }
    void reset() noexcept override {}
};
struct GainSpy final : audition::dsp::ISourceGain {
    std::vector<int>* order{};
    bool nonfinite{false}, out_of_range{false};
    std::vector<float> process(const float* in, std::size_t n, std::uint32_t) override {
        order->push_back(2);
        std::vector<float> out(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = in[i] * 3.0F;
        if (nonfinite) out.front() = std::numeric_limits<float>::infinity();
        if (out_of_range) out.front() = -1.25F;
        return out;
    }
    double gainDb() const noexcept override { return 0.0; }
    void reset() noexcept override {}
};
} // namespace

TEST(SourceSpeechChain, FailureTrackerIsBoundedAndRetiresOnIdReuse) {
    using audition::dsp::SourceSpeechFailureTracker;
    EXPECT_THROW((void)SourceSpeechFailureTracker(0U),
                 std::invalid_argument);
    EXPECT_THROW((void)SourceSpeechFailureTracker(1U, 0U),
                 std::invalid_argument);
    SourceSpeechFailureTracker monitor{2U, 6U};
    monitor.recordFailure(17U, 160U, "NS initialization failed");
    monitor.recordFailure(17U, 80U, "AGC failed");
    monitor.recordFailure(19U, 10U, "ok");
    monitor.recordFailure(23U, 320U, "too many live tracks");
    EXPECT_EQ(monitor.totalFailures(), 4U);
    EXPECT_EQ(monitor.untrackedFailures(), 1U);
    ASSERT_EQ(monitor.active().size(), 2U);
    EXPECT_EQ(monitor.active().at(17U).failed_hops, 2U);
    EXPECT_EQ(monitor.active().at(17U).muted_samples, 240U);
    EXPECT_EQ(monitor.active().at(17U).last_error, "AGC fa");
    monitor.retire(17U);
    monitor.recordFailure(17U, 40U, "new gen");
    EXPECT_EQ(monitor.active().at(17U).failed_hops, 1U);
    EXPECT_EQ(monitor.active().at(17U).muted_samples, 40U);
    EXPECT_EQ(monitor.totalFailures(), 5U);
    monitor.retire(42U);
    monitor.reset();
    EXPECT_TRUE(monitor.active().empty());
    EXPECT_EQ(monitor.totalFailures(), 0U);
    EXPECT_EQ(monitor.untrackedFailures(), 0U);
}

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

TEST(SourceSpeechChain, FailedProcessorMutesHopWithoutLeakingRawPcm) {
    std::vector<int> order;
    NsSpy ns; ns.order = &order;
    GainSpy gain; gain.order = &order;
    const std::vector<float> pcm{0.1F, -0.2F, 0.3F, -0.4F};

    ns.nonfinite = true;
    auto ns_failure = audition::dsp::processSourceSpeechHopOrMute(
        pcm.data(), pcm.size(), 16000U, &ns, &gain);
    EXPECT_TRUE(ns_failure.failed);
    EXPECT_FALSE(ns_failure.error.empty());
    EXPECT_EQ(ns_failure.samples, std::vector<float>(pcm.size(), 0.0F));
    EXPECT_EQ(order, (std::vector<int>{1})); // never invoke AGC on corrupt NS
    ns.nonfinite = false;
    order.clear();

    gain.nonfinite = true;
    auto agc_failure = audition::dsp::processSourceSpeechHopOrMute(
        pcm.data(), pcm.size(), 16000U, &ns, &gain);
    EXPECT_TRUE(agc_failure.failed);
    EXPECT_FALSE(agc_failure.error.empty());
    EXPECT_EQ(agc_failure.samples, std::vector<float>(pcm.size(), 0.0F));
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    gain.nonfinite = false;

    const auto valid = audition::dsp::processSourceSpeechHopOrMute(
        pcm.data(), pcm.size(), 16000U, &ns, &gain);
    EXPECT_FALSE(valid.failed);
    EXPECT_TRUE(valid.error.empty());
    ASSERT_EQ(valid.samples.size(), pcm.size());
    EXPECT_FLOAT_EQ(valid.samples.front(), pcm.front() * 1.5F);

    const auto invalid = audition::dsp::processSourceSpeechHopOrMute(
        nullptr, pcm.size(), 16000U, &ns, &gain);
    EXPECT_TRUE(invalid.failed);
    EXPECT_EQ(invalid.samples, std::vector<float>(pcm.size(), 0.0F));
    EXPECT_EQ(pcm, (std::vector<float>{0.1F, -0.2F, 0.3F, -0.4F}));
    const auto empty = audition::dsp::processSourceSpeechHopOrMute(
        nullptr, 0U, 16000U, &ns, &gain);
    EXPECT_FALSE(empty.failed);
    EXPECT_TRUE(empty.samples.empty());
}

TEST(SourceSpeechChain, RejectsOutOfRangeSamplesBeforeAndAfterAdapters) {
    std::vector<int> order;
    NsSpy ns; ns.order = &order;
    GainSpy gain; gain.order = &order;
    for (const float invalid : {1.01F, -1.01F}) {
        EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
            &invalid, 1U, 16000U, &ns, &gain), std::invalid_argument);
        const auto muted = audition::dsp::processSourceSpeechHopOrMute(
            &invalid, 1U, 16000U, &ns, &gain);
        EXPECT_TRUE(muted.failed);
        EXPECT_EQ(muted.samples, (std::vector<float>{0.0F}));
    }
    EXPECT_TRUE(order.empty());

    const std::vector<float> input(32U, 0.1F);
    ns.out_of_range = true;
    EXPECT_THROW((void)audition::dsp::processSourceSpeechChain(
        input.data(), input.size(), 16000U, &ns, &gain), std::runtime_error);
    EXPECT_EQ(order, (std::vector<int>{1}));
    const auto ns_mute = audition::dsp::processSourceSpeechHopOrMute(
        input.data(), input.size(), 16000U, &ns, &gain);
    EXPECT_TRUE(ns_mute.failed);
    EXPECT_EQ(ns_mute.samples, std::vector<float>(input.size(), 0.0F));
    ns.out_of_range = false;
    order.clear();

    gain.out_of_range = true;
    const auto gain_mute = audition::dsp::processSourceSpeechHopOrMute(
        input.data(), input.size(), 16000U, &ns, &gain);
    EXPECT_TRUE(gain_mute.failed);
    EXPECT_EQ(gain_mute.samples, std::vector<float>(input.size(), 0.0F));
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    gain.out_of_range = false;

    for (const float boundary : {-1.0F, 1.0F}) {
        const auto output = audition::dsp::processSourceSpeechChain(
            &boundary, 1U, 16000U, nullptr, nullptr);
        ASSERT_EQ(output.size(), 1U);
        EXPECT_EQ(output.front(), boundary);
    }
}

TEST(SourceSpeechChain, MatchedRmsAndPeakMetersTrackOnlyProcessedSamples) {
    std::vector<int> order;
    NsSpy ns; ns.order = &order;
    GainSpy gain; gain.order = &order;
    audition::dsp::StreamingLevelMeter raw;
    audition::dsp::StreamingLevelMeter speech;
    const std::vector<float> pcm{0.1F, -0.2F, 0.3F, -0.4F};
    for (int hop = 0; hop < 3; ++hop) {
        auto output = audition::dsp::processSourceSpeechChain(
            pcm.data(), pcm.size(), 16000U, &ns, &gain);
        raw.push(pcm.data(), pcm.size());
        speech.push(output.data(), output.size());
    }
    const auto before = raw.snapshotAndReset();
    const auto after = speech.snapshotAndReset();
    ASSERT_EQ(before.sample_count, 3U * pcm.size());
    ASSERT_EQ(before.sample_count, after.sample_count);
    EXPECT_NEAR(after.rms_dbfs - before.rms_dbfs,
                20.0 * std::log10(1.5), 1.e-5);
    EXPECT_NEAR(after.peak_dbfs - before.peak_dbfs,
                20.0 * std::log10(1.5), 1.e-5);
    EXPECT_EQ(raw.snapshotAndReset().sample_count, 0U);
    EXPECT_EQ(speech.snapshotAndReset().sample_count, 0U);
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
