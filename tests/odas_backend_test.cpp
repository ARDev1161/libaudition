#include <audition/backends/odas/odas_spatial_engine.hpp>

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

namespace {

audition::OdasOptions makeFourMicOptions() {
    audition::OdasOptions options{};
    options.microphone_array.microphones = {
        audition::MicrophoneGeometry{{-0.032, 0.000, 0.000}},
        audition::MicrophoneGeometry{{0.000, -0.032, 0.000}},
        audition::MicrophoneGeometry{{0.032, 0.000, 0.000}},
        audition::MicrophoneGeometry{{0.000, 0.032, 0.000}},
    };
    options.input_channels = {1U, 2U, 3U, 4U};
    return options;
}

audition::AudioBuffer makeSilence(const audition::OdasOptions& options) {
    constexpr std::uint32_t kDeviceChannels = 6U;
    std::vector<float> samples(static_cast<std::size_t>(options.hop_size) * kDeviceChannels, 0.0F);
    return audition::AudioBuffer{std::move(samples),
                                 {options.sample_rate_hz, kDeviceChannels,
                                  audition::AudioLayout::Interleaved},
                                 audition::Timestamp{1, {audition::ClockDomain::Monotonic, 1U}}, 7U};
}

}  // namespace

TEST(OdasOptions, AcceptsTypedFourMicrophoneConfiguration) {
    const auto options = makeFourMicOptions();
    EXPECT_NO_THROW(audition::validateOdasOptions(options));
}

TEST(OdasOptions, AcceptsUpstreamReSpeakerAngularMask) {
    auto options = makeFourMicOptions();
    options.microphone_directivity.assign(
        options.microphone_array.size(),
        audition::OdasMicrophoneDirectivity{80.0, 100.0});
    options.spatial_filters = {audition::OdasSpatialFilter{
        audition::Direction3D::fromVector({0.0, 0.0, 1.0}), 80.0, 100.0}};
    ASSERT_NO_THROW(audition::validateOdasOptions(options));
    // This verifies the ODAS runtime can instantiate and process the profile;
    // it does not imply DOA accuracy with real USB hardware.
    options.sss.enabled = false;
    audition::OdasSpatialEngine engine{options};
    auto frame = makeSilence(options);
    EXPECT_NO_THROW((void)engine.process(frame.view()));
}

TEST(OdasOptions, RejectsDuplicateInputChannelMapping) {
    auto options = makeFourMicOptions();
    options.input_channels = {1U, 1U, 3U, 4U};
    EXPECT_THROW(audition::validateOdasOptions(options), audition::Error);
}

TEST(OdasOptions, RejectsUnsafePotentialSourceCountForScanLevels) {
    auto options = makeFourMicOptions();
    options.ssl.potential_source_count = 1U;
    EXPECT_THROW(audition::validateOdasOptions(options), audition::Error);
}

TEST(OdasOptions, DisabledSeparationDoesNotValidateUnusedPostFilterParameters) {
    auto options = makeFourMicOptions();
    options.sss.enabled = false;
    options.noise.alpha_s = 10.0;
    options.sss.ss_slope = 0.0;
    EXPECT_NO_THROW(audition::validateOdasOptions(options));
}

TEST(OdasBackend, ReportsConfiguredCapabilities) {
    const auto options = makeFourMicOptions();
    audition::OdasSpatialEngine engine{options};
    const auto info = engine.backendInfo();
    EXPECT_EQ(info.name, "odas");

    const auto caps = engine.capabilities();
    EXPECT_TRUE(caps.localization);
    EXPECT_TRUE(caps.tracking);
    EXPECT_TRUE(caps.separation);
    EXPECT_TRUE(caps.supports_3d);
    ASSERT_EQ(caps.audio.supported_sample_rates_hz.size(), 1U);
    EXPECT_EQ(caps.audio.supported_sample_rates_hz.front(), 16000U);
    EXPECT_EQ(caps.audio.min_channels, 5U);
    EXPECT_EQ(caps.audio.preferred_frame_count, 128U);
}

TEST(OdasBackend, RejectsMismatchedInputContract) {
    const auto options = makeFourMicOptions();
    audition::OdasSpatialEngine engine{options};

    std::vector<float> samples(128U * 6U, 0.0F);
    audition::AudioBuffer wrong_rate{std::move(samples),
                                     {48000U, 6U, audition::AudioLayout::Interleaved},
                                     audition::Timestamp{}};
    EXPECT_THROW(engine.process(wrong_rate.view()), audition::Error);
}

TEST(OdasBackend, ProcessesOneConfiguredHopWithoutOwningScheduling) {
    const auto options = makeFourMicOptions();
    audition::OdasSpatialEngine engine{options};
    auto silence = makeSilence(options);

    const auto result = engine.process(silence.view());
    EXPECT_LE(result.tracks.size(), static_cast<std::size_t>(options.sst.max_tracks));
    EXPECT_EQ(result.separated_frames.size(), result.tracks.size());
    EXPECT_LE(result.potential_sources.size(),
              static_cast<std::size_t>(options.ssl.potential_source_count));
    for (const auto& proposal : result.potential_sources) {
        EXPECT_GT(proposal.score, 0.0);
        EXPECT_NEAR(proposal.direction.vector().squaredNorm(), 1.0, 1e-5);
    }
    for (const auto& frame : result.separated_frames) {
        const auto matching_track = std::find_if(
            result.tracks.begin(), result.tracks.end(),
            [&frame](const audition::SpatialTrack& track) { return track.track_id == frame.track_id; });
        EXPECT_NE(matching_track, result.tracks.end());
        EXPECT_EQ(frame.audio.frameCount(), options.hop_size);
        EXPECT_EQ(frame.audio.format().channel_count, 1U);
    }

    EXPECT_NO_THROW(engine.reset());
}
