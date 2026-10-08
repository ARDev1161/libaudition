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
