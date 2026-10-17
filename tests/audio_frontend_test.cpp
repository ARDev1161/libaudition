#include <audition/dsp/audio_frontend.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <algorithm>
#include <cstddef>
#include <utility>
#include <limits>
#include <vector>

TEST(AudioFrontend, RoutesAndCalibratesChannels) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {
        {1U, {2.0, 0.1, 0.0}},
        {0U, {1.0, 0.0, 0.0}},
    };
    audition::dsp::AudioFrontend frontend{config};

    audition::AudioBuffer input{
        {1.0F, 0.2F, 2.0F, 0.3F, 3.0F, 0.4F},
        {16000U, 2U, audition::AudioLayout::Interleaved},
        audition::Timestamp{10, {audition::ClockDomain::Monotonic, 1U}},
        42U};

    const auto output = frontend.process(input.view());
    ASSERT_EQ(output.format().channel_count, 2U);
    ASSERT_EQ(output.frameCount(), 3U);
    EXPECT_NEAR(output.samples()[0], 0.2, 1.0e-6);
    EXPECT_NEAR(output.samples()[1], 1.0, 1.0e-6);
    EXPECT_NEAR(output.samples()[2], 0.4, 1.0e-6);
    EXPECT_NEAR(output.samples()[3], 2.0, 1.0e-6);
    EXPECT_EQ(output.captureTime().nanoseconds(), 10);
    EXPECT_EQ(output.sequenceNumber(), 42U);
}

TEST(AudioFrontend, FractionalDelayKeepsHistoryAcrossBlocks) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 1.5}}};
    audition::dsp::AudioFrontend frontend{config};

    audition::AudioBuffer first{{1.0F, 2.0F, 3.0F},
                                {16000U, 1U, audition::AudioLayout::Interleaved},
                                audition::Timestamp{}};
    const auto first_out = frontend.process(first.view());
    ASSERT_EQ(first_out.samples().size(), 3U);
    EXPECT_NEAR(first_out.samples()[0], 0.0, 1.0e-6);
    EXPECT_NEAR(first_out.samples()[1], 0.5, 1.0e-6);
    EXPECT_NEAR(first_out.samples()[2], 1.5, 1.0e-6);

    audition::AudioBuffer second{{4.0F, 5.0F},
                                 {16000U, 1U, audition::AudioLayout::Interleaved},
                                 audition::Timestamp{}};
    const auto second_out = frontend.process(second.view());
    ASSERT_EQ(second_out.samples().size(), 2U);
    EXPECT_NEAR(second_out.samples()[0], 2.5, 1.0e-6);
    EXPECT_NEAR(second_out.samples()[1], 3.5, 1.0e-6);
}

TEST(AudioFrontend, RejectsMissingInputChannel) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{2U, {}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer input{{0.0F, 0.0F},
                                {16000U, 2U, audition::AudioLayout::Interleaved},
                                audition::Timestamp{}};
    EXPECT_THROW(
        {
            const auto output = frontend.process(input.view());
            (void)output;
        },
        audition::Error);
}


TEST(AudioFrontend, RejectsEmptyRoutesAndInvalidCalibration) {
    audition::dsp::AudioFrontendConfig config{};
    EXPECT_THROW(static_cast<void>(audition::dsp::AudioFrontend{config}), audition::Error);
    config.routes = {{0U, {}}};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double value : {nan, inf, -inf}) {
        config.routes[0].calibration.gain = value;
        EXPECT_THROW(static_cast<void>(audition::dsp::AudioFrontend{config}), audition::Error);
        config.routes[0].calibration.gain = 1.0;
        config.routes[0].calibration.dc_offset = value;
        EXPECT_THROW(static_cast<void>(audition::dsp::AudioFrontend{config}), audition::Error);
        config.routes[0].calibration.dc_offset = 0.0;
        config.routes[0].calibration.delay_samples = value;
        EXPECT_THROW(static_cast<void>(audition::dsp::AudioFrontend{config}), audition::Error);
        config.routes[0].calibration.delay_samples = 0.0;
    }
    config.routes[0].calibration.delay_samples = -0.25;
    EXPECT_THROW(static_cast<void>(audition::dsp::AudioFrontend{config}), audition::Error);
}

TEST(AudioFrontend, RejectsFormatChangesUntilReset) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer mono16{{0.1F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    audition::AudioBuffer mono48{{0.2F},
        {48000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    audition::AudioBuffer stereo48{{0.3F, 0.4F},
        {48000U, 2U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    EXPECT_NO_THROW(static_cast<void>(frontend.process(mono16.view())));
    EXPECT_THROW(static_cast<void>(frontend.process(mono48.view())), audition::Error);
    frontend.reset();
    EXPECT_NO_THROW(static_cast<void>(frontend.process(mono48.view())));
    EXPECT_THROW(static_cast<void>(frontend.process(stereo48.view())), audition::Error);
    frontend.reset();
    const auto output = frontend.process(stereo48.view());
    EXPECT_EQ(output.format().channel_count, 1U);
    ASSERT_EQ(output.samples().size(), 1U);
    EXPECT_FLOAT_EQ(output.samples()[0], 0.3F);
}

TEST(AudioFrontend, CarriesShortBlocksThroughLongDelayHistory) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 3.0}}};
    audition::dsp::AudioFrontend frontend{config};
    for (int i = 1; i <= 5; ++i) {
        audition::AudioBuffer input{{static_cast<float>(i)},
            {16000U, 1U, audition::AudioLayout::Interleaved},
            audition::Timestamp{100 + i, {audition::ClockDomain::Simulation, 1U}},
            static_cast<std::uint64_t>(i)};
        const auto output = frontend.process(input.view());
        ASSERT_EQ(output.samples().size(), 1U);
        EXPECT_FLOAT_EQ(output.samples()[0], i > 3 ? static_cast<float>(i - 3) : 0.F);
        EXPECT_EQ(output.sequenceNumber(), static_cast<std::uint64_t>(i));
    }
    frontend.reset();
    audition::AudioBuffer input{{9.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    EXPECT_FLOAT_EQ(frontend.process(input.view()).samples()[0], 0.F);
}

TEST(AudioFrontend, RoutesPlanarOutputAndHandlesZeroFrameInput) {
    audition::dsp::AudioFrontendConfig config{};
    config.output_layout = audition::AudioLayout::Planar;
    config.routes = {{1U, {}}, {0U, {}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer empty{{},
        {8000U, 2U, audition::AudioLayout::Planar}, audition::Timestamp{}, 19U};
    const auto blank = frontend.process(empty.view());
    EXPECT_TRUE(blank.samples().empty());
    EXPECT_EQ(blank.sequenceNumber(), 19U);
    audition::AudioBuffer input{{1.F, 2.F, 3.F, 4.F},
        {8000U, 2U, audition::AudioLayout::Planar}, audition::Timestamp{}, 20U};
    const auto out = frontend.process(input.view());
    ASSERT_EQ(out.samples().size(), 4U);
    EXPECT_EQ(out.format().layout, audition::AudioLayout::Planar);
    EXPECT_FLOAT_EQ(out.samples()[0], 3.F);
    EXPECT_FLOAT_EQ(out.samples()[1], 4.F);
    EXPECT_FLOAT_EQ(out.samples()[2], 1.F);
    EXPECT_FLOAT_EQ(out.samples()[3], 2.F);
    EXPECT_EQ(out.sequenceNumber(), 20U);
}


TEST(AudioFrontend, DuplicateRoutesApplyIndependentGains) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 0.0}},
                     {0U, {2.0, 0.0, 0.0}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer input{{1.F, 2.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}, 11U};
    const auto result = frontend.process(input.view());
    EXPECT_EQ(result.samples(), (std::vector<float>{1.F, 2.F, 2.F, 4.F}));
    EXPECT_EQ(result.format().channel_count, 2U);
    EXPECT_EQ(result.sequenceNumber(), 11U);
}

TEST(AudioFrontend, NegativeGainAndDcOffsetDoNotInventClipping) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {-2.0, 1.0, 0.0}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer input{{3.F, 5.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto result = frontend.process(input.view());
    EXPECT_EQ(result.samples(), (std::vector<float>{-4.F, -8.F}));
}

TEST(AudioFrontend, FractionalDelayInterpolatesAcrossChunkBoundary) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 0.5}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer initial{{2.F, 4.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto first = frontend.process(initial.view());
    EXPECT_EQ(first.samples(), (std::vector<float>{1.F, 3.F}));
    audition::AudioBuffer subsequent{{6.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto second = frontend.process(subsequent.view());
    EXPECT_EQ(second.samples(), (std::vector<float>{5.F}));
}

TEST(AudioFrontend, EmptyBlockDoesNotEraseDelayedHistory) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 1.0}}};
    audition::dsp::AudioFrontend frontend{config};
    const audition::AudioFormat format{
        16000U, 1U, audition::AudioLayout::Interleaved};
    audition::AudioBuffer initial{{1.F, 2.F}, format, audition::Timestamp{}};
    EXPECT_EQ(frontend.process(initial.view()).samples(),
              (std::vector<float>{0.F, 1.F}));
    audition::AudioBuffer empty{{}, format, audition::Timestamp{}};
    EXPECT_TRUE(frontend.process(empty.view()).samples().empty());
    audition::AudioBuffer next{{3.F}, format, audition::Timestamp{}};
    EXPECT_EQ(frontend.process(next.view()).samples(),
              (std::vector<float>{2.F}));
}

TEST(AudioFrontend, MultiroutePlanarOutputDoesNotMixChannels) {
    audition::dsp::AudioFrontendConfig config{};
    config.output_layout = audition::AudioLayout::Planar;
    config.routes = {{1U, {}}, {1U, {}}, {0U, {}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer input{{1.F, 10.F, 2.F, 20.F},
        {16000U, 2U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto result = frontend.process(input.view());
    EXPECT_EQ(result.samples(),
              (std::vector<float>{10.F, 20.F, 10.F, 20.F, 1.F, 2.F}));
    EXPECT_EQ(result.format().channel_count, 3U);
    EXPECT_EQ(result.format().layout, audition::AudioLayout::Planar);
}

TEST(AudioFrontend, InvalidFirstInputDoesNotLockFormat) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{1U, {}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer mono{{3.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    EXPECT_THROW(static_cast<void>(frontend.process(mono.view())), audition::Error);
    audition::AudioBuffer stereo{{3.F, 4.F},
        {48000U, 2U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto output = frontend.process(stereo.view());
    ASSERT_EQ(output.samples().size(), 1U);
    EXPECT_FLOAT_EQ(output.samples()[0], 4.F);
    EXPECT_EQ(output.format().sample_rate_hz, 48000U);
}

TEST(AudioFrontend, TwoRoutesFromSameChannelKeepSeparateDelayStates) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 0.0}},
                     {0U, {1.0, 0.0, 1.0}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer input{{1.F, 2.F, 3.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto result = frontend.process(input.view());
    EXPECT_EQ(result.samples(),
              (std::vector<float>{1.F, 0.F, 2.F, 1.F, 3.F, 2.F}));
}

TEST(AudioFrontend, ResetClearsFractionalHistoryWithoutChangingRouting) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 0.5}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer first{{2.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    EXPECT_EQ(frontend.process(first.view()).samples(), (std::vector<float>{1.F}));
    frontend.reset();
    audition::AudioBuffer second{{6.F},
        {48000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto output = frontend.process(second.view());
    EXPECT_EQ(output.samples(), (std::vector<float>{3.F}));
    EXPECT_EQ(output.format().sample_rate_hz, 48000U);
}

TEST(AudioFrontend, PassthroughRouteRetainsSampleBeyondFullScale) {
    audition::dsp::AudioFrontendConfig config{};
    config.routes = {{0U, {1.0, 0.0, 0.0}}};
    audition::dsp::AudioFrontend frontend{config};
    audition::AudioBuffer input{{-1.5F, 1.5F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    EXPECT_EQ(frontend.process(input.view()).samples(),
              (std::vector<float>{-1.5F, 1.5F}));
}


namespace {
// Feed the same 3-channel capture in arbitrary interleaved/planar chunks and
// gather the routed output in canonical frame-major order for comparison.
std::vector<float> runFrontendChunks(
    audition::dsp::AudioFrontend& frontend,
    const std::vector<float>& interleaved_pcm,
    audition::AudioLayout input_layout,
    const std::vector<std::size_t>& chunks) {
    constexpr std::size_t kChannels = 3U;
    const auto total_frames = interleaved_pcm.size() / kChannels;
    std::vector<float> collected;
    std::size_t offset = 0U;
    std::uint64_t sequence = 1U;
    for (const auto count : chunks) {
        EXPECT_LE(offset + count, total_frames);
        if (offset + count > total_frames) break;
        std::vector<float> pcm(count * kChannels);
        for (std::size_t frame = 0; frame < count; ++frame) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                const auto index = input_layout == audition::AudioLayout::Interleaved
                    ? frame * kChannels + channel
                    : channel * count + frame;
                pcm[index] = interleaved_pcm[(offset + frame) * kChannels + channel];
            }
        }
        const audition::AudioBuffer block{
            std::move(pcm),
            {16000U, static_cast<std::uint32_t>(kChannels), input_layout},
            audition::Timestamp{}, sequence};
        const auto output = frontend.process(block.view());
        EXPECT_EQ(output.frameCount(), count);
        EXPECT_EQ(output.sequenceNumber(), sequence);
        EXPECT_EQ(output.format().channel_count, frontend.config().routes.size());
        EXPECT_EQ(output.format().layout, frontend.config().output_layout);
        for (std::size_t frame = 0; frame < count; ++frame)
            for (std::size_t channel = 0; channel < output.format().channel_count;
                 ++channel)
                collected.push_back(output.view().channel(channel)[frame]);
        ++sequence;
        offset += count;
    }
    EXPECT_EQ(offset, total_frames);
    return collected;
}

audition::dsp::AudioFrontendConfig chunkInvariantFrontendConfig(
    audition::AudioLayout output_layout) {
    audition::dsp::AudioFrontendConfig config{};
    config.output_layout = output_layout;
    config.routes = {
        {2U, {0.75, 0.125, 0.0}},
        {0U, {-1.25, -0.0625, 1.25}},
        {1U, {1.0, 0.0, 5.5}},
    };
    return config;
}
} // namespace

TEST(AudioFrontend, ChunkPartitioningPreservesCalibratedMultichannelSamples) {
    std::vector<float> pcm;
    constexpr std::size_t kFrames = 37U;
    for (std::size_t frame = 0; frame < kFrames; ++frame)
        for (std::size_t channel = 0; channel < 3U; ++channel)
            pcm.push_back(static_cast<float>(
                static_cast<int>((frame * 3U + channel * 7U) % 23U) - 11) / 16.F);

    // Includes blocks shorter than delay history, boundaries at fractional
    // delays, a zero-frame block and both input and output layouts.
    for (const auto input_layout : {audition::AudioLayout::Interleaved,
                                    audition::AudioLayout::Planar}) {
        for (const auto output_layout : {audition::AudioLayout::Interleaved,
                                         audition::AudioLayout::Planar}) {
            const auto config = chunkInvariantFrontendConfig(output_layout);
            audition::dsp::AudioFrontend monolithic{config};
            const auto expected = runFrontendChunks(
                monolithic, pcm, input_layout, {kFrames});
            for (const auto& partition : {
                    std::vector<std::size_t>{1U, 2U, 3U, 5U, 8U, 13U, 5U},
                    std::vector<std::size_t>{0U, 1U, 1U, 1U, 1U, 1U, 1U,
                                             1U, 1U, 1U, 1U, 27U}}) {
                audition::dsp::AudioFrontend fragmented{config};
                const auto actual = runFrontendChunks(
                    fragmented, pcm, input_layout, partition);
                ASSERT_EQ(actual.size(), expected.size());
                for (std::size_t i = 0; i < actual.size(); ++i)
                    EXPECT_NEAR(actual[i], expected[i], 1.0e-6) << "sample " << i;
            }
        }
    }
}

TEST(AudioFrontend, ResetMatchesFreshInstanceAfterFragmentedHistory) {
    const auto config = chunkInvariantFrontendConfig(audition::AudioLayout::Planar);
    audition::dsp::AudioFrontend reused{config};
    audition::dsp::AudioFrontend fresh{config};
    const std::vector<float> prior = {0.F, 1.F, 2.F, 3.F, 4.F, 5.F,
                                      6.F, 7.F, 8.F, 9.F, 10.F, 11.F};
    static_cast<void>(runFrontendChunks(
        reused, prior, audition::AudioLayout::Interleaved, {1U, 1U, 2U}));
    reused.reset();

    const std::vector<float> new_pcm = {
        10.F, -10.F, 0.F, 11.F, -11.F, 1.F,
        12.F, -12.F, 2.F, 13.F, -13.F, 3.F,
        14.F, -14.F, 4.F, 15.F, -15.F, 5.F};
    const auto expected = runFrontendChunks(
        fresh, new_pcm, audition::AudioLayout::Planar, {6U});
    const auto actual = runFrontendChunks(
        reused, new_pcm, audition::AudioLayout::Planar, {1U, 2U, 0U, 3U});
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_NEAR(actual[i], expected[i], 1.0e-6) << "sample " << i;
}
