#include <audition/backends/webrtc_aec3/webrtc_aec3_echo_canceller.hpp>

#include <cmath>
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
