#include <audition/backends/odas/odas_spatial_engine.hpp>
#include <audition/backends/odas/source_elevation_prior.hpp>
#include <audition/backends/odas/respeaker_usb4_profile.hpp>
#include <audition/backends/odas/quiet_source_profile.hpp>

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


TEST(OdasOptions, QuietSourceTrackingProfileOnlyChangesSstLikelihood) {
    auto options = makeFourMicOptions();
    const auto baselineSsl = options.ssl.minimum_probability;
    const auto baselineNew = options.sst.new_threshold;
    const auto baselineProbability = options.sst.probability_threshold;
    const auto baselineTimeout = options.sst.inactive_timeout_hops;
    const auto baselineActive = options.sst.active_gmm.front().mean;
    const auto baselineInactive = options.sst.inactive_gmm.front().mean;

    EXPECT_DOUBLE_EQ(baselineActive, 0.4);
    EXPECT_DOUBLE_EQ(baselineInactive, 0.25);
    audition::applyOdasQuietSourceTrackingProfile(options);
    EXPECT_DOUBLE_EQ(options.sst.active_gmm.front().mean, 0.3);
    EXPECT_DOUBLE_EQ(options.sst.inactive_gmm.front().mean, 0.15);
    EXPECT_DOUBLE_EQ(options.ssl.minimum_probability, baselineSsl);
    EXPECT_DOUBLE_EQ(options.sst.new_threshold, baselineNew);
    EXPECT_DOUBLE_EQ(options.sst.probability_threshold, baselineProbability);
    EXPECT_EQ(options.sst.inactive_timeout_hops, baselineTimeout);
    EXPECT_NO_THROW(audition::validateOdasOptions(options));

    // Construct and process with the actual libodas backend on silence.
    // This is an integration/contract gate, not a quiet speech accuracy gate.
    options.sss.enabled = false;
    audition::OdasSpatialEngine engine{options};
    auto frame = makeSilence(options);
    EXPECT_NO_THROW((void)engine.process(frame.view()));
}

TEST(OdasOptions, QuietSourceTrackingRejectsMissingLikelihoodModels) {
    auto options = makeFourMicOptions();
    options.sst.active_gmm.clear();
    EXPECT_THROW(audition::applyOdasQuietSourceTrackingProfile(options),
                 std::invalid_argument);
}

TEST(OdasOptions, NearPlanePriorRequiresExplicitSelection) {
    auto options = makeFourMicOptions();
    const auto initialFilterCount = options.spatial_filters.size();
    EXPECT_EQ(initialFilterCount, 1U);

    audition::applyReSpeakerUsb4MicAngularProfile(options);
    EXPECT_EQ(options.microphone_directivity.size(), 4U);
    audition::applyOdasNearPlaneSourcePrior(options);
    ASSERT_EQ(options.spatial_filters.size(), 2U);
    EXPECT_NEAR(options.spatial_filters[0].direction.vector().z, 1.0, 1e-8);
    EXPECT_NEAR(options.spatial_filters[1].direction.vector().z, -1.0, 1e-8);
    EXPECT_DOUBLE_EQ(options.spatial_filters[0].all_pass_angle_deg, 110.0);
    EXPECT_DOUBLE_EQ(options.spatial_filters[0].no_pass_angle_deg, 115.0);
    EXPECT_DOUBLE_EQ(options.spatial_filters[1].all_pass_angle_deg, 110.0);
    EXPECT_DOUBLE_EQ(options.spatial_filters[1].no_pass_angle_deg, 115.0);
    EXPECT_NO_THROW(audition::validateOdasOptions(options));
}

TEST(OdasOptions, NearPlanePriorRejectsInvalidBounds) {
    auto options = makeFourMicOptions();
    EXPECT_THROW(audition::applyOdasNearPlaneSourcePrior(options, 0.0), std::invalid_argument);
    EXPECT_THROW(audition::applyOdasNearPlaneSourcePrior(options, 90.0), std::invalid_argument);
    EXPECT_THROW(audition::applyOdasNearPlaneSourcePrior(options, 25.0, 25.0), std::invalid_argument);
    EXPECT_THROW(audition::applyOdasNearPlaneSourcePrior(options, 25.0, -1.0), std::invalid_argument);
    EXPECT_EQ(options.spatial_filters.size(), 1U);
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
    EXPECT_THROW((void)engine.process(wrong_rate.view()), audition::Error);
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
