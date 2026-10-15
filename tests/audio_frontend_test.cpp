#include <audition/dsp/audio_frontend.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

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
