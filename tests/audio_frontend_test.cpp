#include <audition/dsp/audio_frontend.hpp>

#include <gtest/gtest.h>

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
    audition::dsp::AudioFrontend frontend{{{{2U, {}}}, audition::AudioLayout::Interleaved}};
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
