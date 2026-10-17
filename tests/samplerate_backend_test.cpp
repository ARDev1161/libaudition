#include <audition/backends/samplerate/samplerate_resampler.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<float> sine(std::uint32_t sample_rate, std::size_t frames,
                        std::uint32_t channels) {
    std::vector<float> samples(frames * channels);
    constexpr double kPi = 3.14159265358979323846;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(
            0.5 * std::sin(2.0 * kPi * 440.0 *
                           static_cast<double>(frame) /
                           static_cast<double>(sample_rate)));
        for (std::size_t channel = 0; channel < channels; ++channel) {
            samples[frame * channels + channel] = value;
        }
    }
    return samples;
}

}  // namespace

TEST(SamplerateBackend, StreamsAndFlushes) {
    audition::SamplerateResampler engine{};
    auto session = engine.createSession({48000U, 16000U, 2U});

    audition::AudioBuffer input{
        sine(48000U, 4800U, 2U),
        {48000U, 2U, audition::AudioLayout::Interleaved},
        audition::Timestamp{1000, {audition::ClockDomain::Monotonic, 1U}},
        5U};

    const auto output = session->process(input.view());
    const auto tail = session->flush();

    EXPECT_EQ(output.format().sample_rate_hz, 16000U);
    EXPECT_EQ(output.format().channel_count, 2U);
    EXPECT_EQ(output.format().layout, audition::AudioLayout::Interleaved);
    EXPECT_GT(output.frameCount() + tail.frameCount(), 1500U);
    EXPECT_LT(output.frameCount() + tail.frameCount(), 1700U);
    EXPECT_EQ(output.captureTime().nanoseconds(), 1000);
}

TEST(SamplerateBackend, PreservesPlanarLayout) {
    audition::SamplerateResampler engine{{audition::SamplerateConverter::Linear}};
    auto session = engine.createSession({16000U, 8000U, 1U});

    audition::AudioBuffer input{
        sine(16000U, 160U, 1U),
        {16000U, 1U, audition::AudioLayout::Planar},
        audition::Timestamp{}};

    const auto output = session->process(input.view());
    EXPECT_EQ(output.format().layout, audition::AudioLayout::Planar);
    EXPECT_GT(output.frameCount(), 0U);
}

TEST(SamplerateBackend, ProcessAfterFlushRequiresReset) {
    audition::SamplerateResampler engine{};
    auto session = engine.createSession({16000U, 8000U, 1U});
    audition::AudioBuffer input{
        sine(16000U, 160U, 1U),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};

    const auto output = session->process(input.view());
    (void)output;
    const auto tail = session->flush();
    (void)tail;

    EXPECT_THROW(
        {
            const auto invalid = session->process(input.view());
            (void)invalid;
        },
        audition::Error);

    EXPECT_NO_THROW(session->reset());
    EXPECT_NO_THROW({
        const auto again = session->process(input.view());
        (void)again;
    });
}


TEST(SamplerateBackend, RepeatedFlushNeverReplaysTailAndResetStartsNewClock) {
    audition::SamplerateResampler engine{{audition::SamplerateConverter::Linear}};
    auto session = engine.createSession({16000U, 8000U, 1U});
    const audition::AudioBuffer first{
        sine(16000U, 320U, 1U),
        {16000U, 1U, audition::AudioLayout::Planar},
        audition::Timestamp{1234567, {audition::ClockDomain::Monotonic, 1U}},
        17U};
    static_cast<void>(session->process(first.view()));
    const auto tail = session->flush();
    const auto again = session->flush();
    EXPECT_TRUE(again.samples().empty());
    EXPECT_EQ(again.frameCount(), 0U);
    EXPECT_EQ(again.format().sample_rate_hz, 8000U);
    EXPECT_EQ(again.format().layout, audition::AudioLayout::Planar);
    EXPECT_EQ(again.sequenceNumber(), 17U);
    EXPECT_EQ(again.captureTime().nanoseconds(),
              tail.captureTime().advancedBy(tail.duration()).nanoseconds());

    session->reset();
    const audition::AudioBuffer second{
        sine(16000U, 320U, 1U),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{7654321, {audition::ClockDomain::Simulation, 9U}},
        99U};
    const auto output = session->process(second.view());
    EXPECT_EQ(output.captureTime().nanoseconds(), 7654321);
    EXPECT_EQ(output.captureTime().clock(), second.captureTime().clock());
    EXPECT_EQ(output.sequenceNumber(), 99U);
    EXPECT_EQ(output.format().layout, audition::AudioLayout::Interleaved);
    EXPECT_GT(output.frameCount(), 0U);
}


TEST(SamplerateBackend, StreamingOutputClockIgnoresLaterInputTimestampJitter) {
    audition::SamplerateResampler engine{{audition::SamplerateConverter::Linear}};
    auto session = engine.createSession({16000U, 8000U, 1U});
    const audition::AudioBuffer first{
        sine(16000U, 320U, 1U),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{1000000000, {audition::ClockDomain::Monotonic, 1U}},
        1U};
    const audition::AudioBuffer second{
        sine(16000U, 320U, 1U),
        {16000U, 1U, audition::AudioLayout::Interleaved},
        // Input timestamp jitter must not introduce a gap in output time.
        audition::Timestamp{9000000000, {audition::ClockDomain::Monotonic, 1U}},
        2U};
    const auto out_first = session->process(first.view());
    const auto out_second = session->process(second.view());
    EXPECT_GT(out_first.frameCount(), 0U);
    EXPECT_GT(out_second.frameCount(), 0U);
    EXPECT_EQ(out_first.captureTime().nanoseconds(), 1000000000);
    EXPECT_EQ(out_second.captureTime().nanoseconds(),
              out_first.captureTime().advancedBy(out_first.duration()).nanoseconds());
    EXPECT_EQ(out_second.sequenceNumber(), 2U);
    const auto tail = session->flush();
    EXPECT_EQ(tail.captureTime().nanoseconds(),
              out_second.captureTime().advancedBy(out_second.duration()).nanoseconds());
}
