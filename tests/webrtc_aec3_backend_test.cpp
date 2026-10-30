#include <audition/backends/webrtc_aec3/webrtc_aec3_echo_canceller.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

audition::AudioBuffer frame(float value, std::uint64_t sequence = 0U) {
    return audition::AudioBuffer{
        std::vector<float>(160U, value),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{static_cast<std::int64_t>(sequence) * 10'000'000,
                            {audition::ClockDomain::Monotonic, 1U}},
        sequence};
}

}  // namespace

TEST(WebRtcAec3Backend, PublicIdentifierRemainsStableAcrossApmMigration) {
    const audition::WebRtcAec3EchoCanceller engine{};
    EXPECT_EQ(engine.backendInfo().name, "webrtc-aec3");
}

TEST(WebRtcAec3Backend, EnforcesTenMillisecondFrames) {
    audition::WebRtcAec3EchoCanceller engine{};
    auto session = engine.createSession(
        {16000U, 1U, audition::AudioLayout::Interleaved},
        {16000U, 1U, audition::AudioLayout::Interleaved});

    audition::AudioBuffer invalid{
        std::vector<float>(159U, 0.0F),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};

    EXPECT_THROW(session->process(invalid.view()), audition::Error);
}

TEST(WebRtcAec3Backend, ProcessesReferenceAndCaptureFrames) {
    audition::WebRtcAec3EchoCanceller engine{};
    auto session = engine.createSession(
        {16000U, 1U, audition::AudioLayout::Interleaved},
        {16000U, 1U, audition::AudioLayout::Interleaved});

    session->setStreamDelay(audition::Duration{50'000'000});
    session->acceptReference(frame(0.1F, 1U).view());
    const auto output = session->process(frame(0.2F, 1U).view());

    EXPECT_EQ(output.frameCount(), 160U);
    EXPECT_EQ(output.format().sample_rate_hz, 16000U);
    EXPECT_EQ(output.format().channel_count, 1U);
    EXPECT_EQ(output.sequenceNumber(), 1U);

    const auto metrics = session->metrics();
    if (metrics.estimated_delay.has_value()) {
        EXPECT_GE(metrics.estimated_delay->nanoseconds(), 0);
    }
}

TEST(WebRtcAec3Backend, ResetPreservesConfiguredDelay) {
    audition::WebRtcAec3EchoCanceller engine{};
    auto session = engine.createSession(
        {16000U, 1U, audition::AudioLayout::Interleaved},
        {16000U, 1U, audition::AudioLayout::Interleaved});

    session->setStreamDelay(audition::Duration{25'000'000});
    EXPECT_NO_THROW(session->reset());
    session->acceptReference(frame(0.0F).view());
    EXPECT_NO_THROW({
        const auto output = session->process(frame(0.0F).view());
        (void)output;
    });
}


namespace {

audition::AudioBuffer tonalFrame(
    std::uint32_t sample_rate_hz, std::uint32_t channels,
    audition::AudioLayout layout, std::size_t frame_index, float amplitude) {
    const auto frames = static_cast<std::size_t>(sample_rate_hz / 100U);
    std::vector<float> samples(frames * channels, 0.F);
    constexpr double kTwoPi = 6.2831853071795864769;
    for (std::size_t i = 0; i < frames; ++i) {
        const auto phase = kTwoPi * 440.0 *
            static_cast<double>(frame_index * frames + i) /
            static_cast<double>(sample_rate_hz);
        for (std::size_t ch = 0; ch < channels; ++ch) {
            const auto value = static_cast<float>(static_cast<double>(amplitude) *
                std::sin(phase + static_cast<double>(ch) * 0.4));
            const auto offset = layout == audition::AudioLayout::Interleaved
                ? i * channels + ch : ch * frames + i;
            samples[offset] = value;
        }
    }
    return audition::AudioBuffer{
        std::move(samples),
        {sample_rate_hz, channels, layout},
        audition::Timestamp{
            static_cast<std::int64_t>(frame_index) * 10'000'000,
            {audition::ClockDomain::Monotonic, 1U}},
        static_cast<std::uint64_t>(frame_index + 1U)};
}

double meanSquare(const audition::AudioBuffer& audio) {
    if (audio.samples().empty()) return 0.0;
    double squares = 0.0;
    for (const float value : audio.samples())
        squares += static_cast<double>(value) * static_cast<double>(value);
    return squares / static_cast<double>(audio.samples().size());
}

void assertBoundedOutput(const audition::AudioBuffer& audio, float max_amplitude) {
    for (const auto sample : audio.samples()) {
        ASSERT_TRUE(std::isfinite(sample));
        EXPECT_LE(std::abs(sample), max_amplitude);
    }
}

} // namespace

TEST(WebRtcAec3Backend, SupportsThreeRatesAndPlanarMultichannelCapture) {
    audition::WebRtcAec3EchoCanceller engine{};
    for (const auto rate : {16000U, 32000U, 48000U}) {
        const audition::AudioFormat capture{
            rate, 2U, audition::AudioLayout::Planar};
        const audition::AudioFormat render{
            rate, 1U, audition::AudioLayout::Interleaved};
        auto session = engine.createSession(capture, render);
        for (std::size_t index = 0; index < 5U; ++index) {
            const auto reference = tonalFrame(
                rate, 1U, audition::AudioLayout::Interleaved, index, 0.0F);
            const auto near_end = tonalFrame(
                rate, 2U, audition::AudioLayout::Planar, index, 0.2F);
            session->acceptReference(reference.view());
            const auto output = session->process(near_end.view());
            EXPECT_EQ(output.frameCount(), static_cast<std::size_t>(rate / 100U));
            EXPECT_EQ(output.format().sample_rate_hz, rate);
            EXPECT_EQ(output.format().channel_count, 2U);
            EXPECT_EQ(output.format().layout, audition::AudioLayout::Planar);
            EXPECT_EQ(output.sequenceNumber(), near_end.sequenceNumber());
            EXPECT_EQ(output.captureTime().nanoseconds(),
                      near_end.captureTime().nanoseconds());
            assertBoundedOutput(output, 1.0001F);
        }
    }
}

TEST(WebRtcAec3Backend, ReferenceWithoutCaptureDoesNotCreateAudibleNearEnd) {
    audition::WebRtcAec3EchoCanceller engine{};
    const audition::AudioFormat format{
        16000U, 1U, audition::AudioLayout::Interleaved};
    auto session = engine.createSession(format, format);
    double maximum_tail_rms = 0.0;
    // Pure far-end playback must not create an audible near-end signal in
    // a microphone capture that is exactly silent. This is an amplitude
    // invariant, not an echo suppression accuracy/ERLE claim.
    for (std::size_t index = 0; index < 40U; ++index) {
        const auto playback = tonalFrame(
            16000U, 1U, audition::AudioLayout::Interleaved, index, 0.2F);
        const auto silent_capture = tonalFrame(
            16000U, 1U, audition::AudioLayout::Interleaved, index, 0.0F);
        session->acceptReference(playback.view());
        const auto output = session->process(silent_capture.view());
        assertBoundedOutput(output, 1.0001F);
        if (index >= 20U)
            maximum_tail_rms = std::max(maximum_tail_rms,
                                        std::sqrt(meanSquare(output)));
    }
    EXPECT_LT(maximum_tail_rms, 0.01)
        << "AEC3 generated near-end audio during silent capture";
}

TEST(WebRtcAec3Backend, NearEndWithoutPlaybackRemainsDetectable) {
    audition::WebRtcAec3EchoCanceller engine{};
    const audition::AudioFormat format{
        16000U, 1U, audition::AudioLayout::Interleaved};
    auto session = engine.createSession(format, format);
    double tail_energy = 0.0;
    std::size_t tail_frames = 0U;
    for (std::size_t index = 0; index < 80U; ++index) {
        const auto silent_playback = tonalFrame(
            16000U, 1U, audition::AudioLayout::Interleaved, index, 0.0F);
        const auto near_end = tonalFrame(
            16000U, 1U, audition::AudioLayout::Interleaved, index, 0.20F);
        session->acceptReference(silent_playback.view());
        const auto output = session->process(near_end.view());
        assertBoundedOutput(output, 1.0001F);
        if (index >= 40U) {
            tail_energy += meanSquare(output);
            ++tail_frames;
        }
    }
    ASSERT_GT(tail_frames, 0U);
    const auto tail_rms = std::sqrt(tail_energy / static_cast<double>(tail_frames));
    // AEC must not mute real near-end audio merely because render is silent.
    // The gate allows internal AGC/suppression, but rejects near-silence.
    EXPECT_GT(tail_rms, 0.015);
}

TEST(WebRtcAec3Backend, RejectsInvalidRatesFramesAndDelaysThenProcessesValidAudio) {
    audition::WebRtcAec3EchoCanceller engine{};
    const audition::AudioFormat mono16{
        16000U, 1U, audition::AudioLayout::Interleaved};
    EXPECT_THROW(
        static_cast<void>(engine.createSession(
            mono16, {32000U, 1U, audition::AudioLayout::Interleaved})),
        audition::Error);
    EXPECT_THROW(
        static_cast<void>(engine.createSession(
            {8000U, 1U, audition::AudioLayout::Interleaved},
            {8000U, 1U, audition::AudioLayout::Interleaved})),
        audition::Error);

    auto session = engine.createSession(mono16, mono16);
    EXPECT_THROW(session->setStreamDelay(audition::Duration{-1}), audition::Error);
    const auto too_large_ms = static_cast<std::int64_t>(
        std::numeric_limits<int>::max()) + 1;
    EXPECT_THROW(session->setStreamDelay(
        audition::Duration{too_large_ms * 1'000'000}), audition::Error);
    const audition::AudioBuffer wrong_render{
        std::vector<float>(159U, 0.0F), mono16, audition::Timestamp{}};
    EXPECT_THROW(session->acceptReference(wrong_render.view()), audition::Error);
    const auto wrong_capture = tonalFrame(
        32000U, 1U, audition::AudioLayout::Interleaved, 0U, 0.2F);
    EXPECT_THROW(
        static_cast<void>(session->process(wrong_capture.view())),
        audition::Error);

    const auto render = frame(0.0F, 17U);
    const auto capture = frame(0.2F, 17U);
    session->acceptReference(render.view());
    const auto output = session->process(capture.view());
    EXPECT_EQ(output.sequenceNumber(), 17U);
    EXPECT_EQ(output.frameCount(), 160U);
    assertBoundedOutput(output, 1.0001F);
}
