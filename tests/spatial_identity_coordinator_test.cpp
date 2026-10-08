#include <audition/pipeline/spatial_identity.hpp>

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

namespace {

audition::Timestamp ts(
    std::int64_t nanoseconds,
    std::uint32_t source_id = 0U) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, source_id}};
}

audition::SpatialTrack trackAt(
    std::uint64_t track_id,
    std::int64_t first_seen,
    std::int64_t last_seen) {
    audition::SpatialTrack track{};
    track.track_id = audition::SpatialTrackId{track_id};
    track.first_seen = ts(first_seen);
    track.last_seen = ts(last_seen);
    return track;
}

class FakeFusion final : public audition::ISpatialFusion {
public:
    audition::BackendInfo backendInfo() const override {
        return {"fake-fusion", "1"};
    }

    std::optional<audition::PositionEstimate> fuse(
        const audition::SpatialFusionInput& input) const override {
        ++calls;
        last_bearing_count = input.bearings.size();
        last_range_count = input.ranges.size();
        last_position_count = input.positions.size();
        return result;
    }

    mutable std::size_t calls{0U};
    mutable std::size_t last_bearing_count{0U};
    mutable std::size_t last_range_count{0U};
    mutable std::size_t last_position_count{0U};
    std::optional<audition::PositionEstimate> result{};
};

class CapturingResolver final : public audition::ISourceIdentityResolver {
public:
    void reset() override {
        ++reset_calls;
    }

    audition::SourceIdentityDecision observe(
        const audition::SourceIdentityObservation& observation) override {
        ++observe_calls;
        last_observation = observation;
        return decision;
    }

    void endTrack(
        audition::SpatialTrackId track_id,
        audition::Timestamp timestamp) override {
        ++end_calls;
        last_ended_track = track_id;
        last_end_timestamp = timestamp;
    }

    audition::SourceIdentityDecision decision{
        audition::AcousticSourceId{42U},
        audition::Probability::from(0.9),
        false};

    std::size_t observe_calls{0U};
    std::size_t reset_calls{0U};
    std::size_t end_calls{0U};
    std::optional<audition::SourceIdentityObservation> last_observation{};
    audition::SpatialTrackId last_ended_track{};
    audition::Timestamp last_end_timestamp{};
};

audition::PositionEstimate position(
    double x,
    double y,
    double z) {
    audition::PositionEstimate result{};
    result.mean_m = {x, y, z};
    return result;
}

}  // namespace

TEST(SpatialIdentityCoordinator, FusesPositionBeforeIdentityResolution) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    fusion.result = position(2.0, 1.0, 0.5);

    audition::PositionObservation prior[] = {
        {ts(90), position(1.8, 1.1, 0.4)},
    };

    auto track = trackAt(7U, 10, 100);
    const auto decision = coordinator.observe(track, {{}, {}, prior});

    EXPECT_EQ(fusion.calls, 1U);
    EXPECT_EQ(fusion.last_position_count, 1U);
    ASSERT_TRUE(track.position.has_value());
    EXPECT_DOUBLE_EQ(track.position->mean_m.x, 2.0);
    EXPECT_DOUBLE_EQ(track.position->mean_m.y, 1.0);
    EXPECT_DOUBLE_EQ(track.position->mean_m.z, 0.5);

    ASSERT_TRUE(resolver.last_observation.has_value());
    ASSERT_TRUE(resolver.last_observation->position.has_value());
    EXPECT_DOUBLE_EQ(
        resolver.last_observation->position->mean_m.x,
        2.0);
    EXPECT_EQ(resolver.last_observation->track_id, track.track_id);
    EXPECT_EQ(
        resolver.last_observation->timestamp.nanoseconds(),
        track.last_seen.nanoseconds());

    ASSERT_TRUE(track.source_id.has_value());
    EXPECT_EQ(*track.source_id, decision.source_id);
    ASSERT_TRUE(coordinator.activeSource(track.track_id).has_value());
    EXPECT_EQ(
        *coordinator.activeSource(track.track_id),
        decision.source_id);
}

TEST(SpatialIdentityCoordinator, EmptyFusionInputPreservesTrackPosition) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    auto track = trackAt(1U, 0, 10);
    track.position = position(1.0, 2.0, 3.0);

    static_cast<void>(coordinator.observe(track));

    EXPECT_EQ(fusion.calls, 0U);
    ASSERT_TRUE(track.position.has_value());
    EXPECT_DOUBLE_EQ(track.position->mean_m.x, 1.0);
    ASSERT_TRUE(resolver.last_observation.has_value());
    ASSERT_TRUE(resolver.last_observation->position.has_value());
    EXPECT_DOUBLE_EQ(
        resolver.last_observation->position->mean_m.y,
        2.0);
}

TEST(SpatialIdentityCoordinator, UnderconstrainedFusionKeepsExistingPosition) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    fusion.result = std::nullopt;

    audition::BearingObservation bearings[] = {
        {
            ts(5),
            {},
            {},
        },
    };

    auto track = trackAt(2U, 0, 10);
    track.position = position(4.0, 5.0, 6.0);

    static_cast<void>(coordinator.observe(track, {bearings, {}, {}}));

    EXPECT_EQ(fusion.calls, 1U);
    ASSERT_TRUE(track.position.has_value());
    EXPECT_DOUBLE_EQ(track.position->mean_m.z, 6.0);
    ASSERT_TRUE(resolver.last_observation.has_value());
    ASSERT_TRUE(resolver.last_observation->position.has_value());
    EXPECT_DOUBLE_EQ(
        resolver.last_observation->position->mean_m.x,
        4.0);
}

TEST(SpatialIdentityCoordinator, RejectsFusionClockMismatchBeforeBackends) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    audition::PositionObservation positions[] = {
        {ts(10, 99U), position(1.0, 0.0, 0.0)},
    };

    auto track = trackAt(3U, 0, 10);

    try {
        static_cast<void>(
            coordinator.observe(track, {{}, {}, positions}));
        FAIL() << "Expected clock mismatch";
    } catch (const audition::Error& error) {
        EXPECT_EQ(
            error.code(),
            audition::ErrorCode::ClockDomainMismatch);
    }

    EXPECT_EQ(fusion.calls, 0U);
    EXPECT_EQ(resolver.observe_calls, 0U);
}

TEST(SpatialIdentityCoordinator, RejectsFutureFusionObservation) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    audition::PositionObservation positions[] = {
        {ts(11), position(1.0, 0.0, 0.0)},
    };

    auto track = trackAt(4U, 0, 10);

    EXPECT_THROW(
        static_cast<void>(
            coordinator.observe(track, {{}, {}, positions})),
        audition::Error);

    EXPECT_EQ(fusion.calls, 0U);
    EXPECT_EQ(resolver.observe_calls, 0U);
}

TEST(SpatialIdentityCoordinator, PropagatesIdentityToSpatialResult) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    auto observed = trackAt(5U, 0, 10);
    static_cast<void>(coordinator.observe(observed));

    audition::SpatialProcessingResult result{};
    result.tracks.push_back(trackAt(5U, 0, 10));
    result.tracks.push_back(trackAt(99U, 0, 10));

    audition::TrackedAudioFrame frame{};
    frame.track_id = audition::SpatialTrackId{5U};
    result.separated_frames.push_back(frame);

    audition::TrackedAudioFrame unknown_frame{};
    unknown_frame.track_id = audition::SpatialTrackId{99U};
    result.separated_frames.push_back(unknown_frame);

    EXPECT_EQ(coordinator.propagate(result), 2U);

    ASSERT_TRUE(result.tracks[0].source_id.has_value());
    EXPECT_EQ(
        *result.tracks[0].source_id,
        audition::AcousticSourceId{42U});
    EXPECT_FALSE(result.tracks[1].source_id.has_value());

    ASSERT_TRUE(result.separated_frames[0].source_id.has_value());
    EXPECT_EQ(
        *result.separated_frames[0].source_id,
        audition::AcousticSourceId{42U});
    EXPECT_FALSE(result.separated_frames[1].source_id.has_value());

    EXPECT_EQ(coordinator.propagate(result), 0U);
}

TEST(SpatialIdentityCoordinator, RejectsConflictingSpatialPropagation) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    auto observed = trackAt(6U, 0, 10);
    static_cast<void>(coordinator.observe(observed));

    audition::TrackedAudioFrame frame{};
    frame.track_id = observed.track_id;
    frame.source_id = audition::AcousticSourceId{999U};

    EXPECT_THROW(
        static_cast<void>(coordinator.propagate(frame)),
        audition::Error);
}

TEST(SpatialIdentityCoordinator, EndTrackForwardsAndClearsActiveBinding) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    auto track = trackAt(7U, 0, 10);
    static_cast<void>(coordinator.observe(track));

    coordinator.endTrack(track.track_id, ts(20));

    EXPECT_EQ(resolver.end_calls, 1U);
    EXPECT_EQ(resolver.last_ended_track, track.track_id);
    EXPECT_EQ(resolver.last_end_timestamp.nanoseconds(), 20);
    EXPECT_FALSE(coordinator.activeSource(track.track_id).has_value());

    audition::TrackedAudioFrame frame{};
    frame.track_id = track.track_id;
    EXPECT_FALSE(coordinator.propagate(frame));
}

TEST(SpatialIdentityCoordinator, ResetForwardsAndClearsBindings) {
    FakeFusion fusion;
    CapturingResolver resolver;
    audition::SpatialIdentityCoordinator coordinator{fusion, resolver};

    auto track = trackAt(8U, 0, 10);
    static_cast<void>(coordinator.observe(track));

    coordinator.reset();

    EXPECT_EQ(resolver.reset_calls, 1U);
    EXPECT_FALSE(coordinator.activeSource(track.track_id).has_value());
}

TEST(SpatialIdentityCoordinator, AttachesResolvedTrackToEvent) {
    auto track = trackAt(9U, 0, 10);
    track.source_id = audition::AcousticSourceId{123U};

    audition::AcousticEvent event{};
    audition::SpatialIdentityCoordinator::attachToEvent(event, track);

    ASSERT_TRUE(event.track_id.has_value());
    ASSERT_TRUE(event.source_id.has_value());
    EXPECT_EQ(*event.track_id, track.track_id);
    EXPECT_EQ(*event.source_id, *track.source_id);

    audition::SpatialIdentityCoordinator::attachToEvent(event, track);
}

TEST(SpatialIdentityCoordinator, RejectsEventIdentityConflict) {
    auto track = trackAt(10U, 0, 10);
    track.source_id = audition::AcousticSourceId{123U};

    audition::AcousticEvent wrong_track{};
    wrong_track.track_id = audition::SpatialTrackId{11U};
    EXPECT_THROW(
        audition::SpatialIdentityCoordinator::attachToEvent(
            wrong_track,
            track),
        audition::Error);

    audition::AcousticEvent wrong_source{};
    wrong_source.track_id = track.track_id;
    wrong_source.source_id = audition::AcousticSourceId{999U};
    EXPECT_THROW(
        audition::SpatialIdentityCoordinator::attachToEvent(
            wrong_source,
            track),
        audition::Error);
}

TEST(SpatialIdentityCoordinator, RejectsUnresolvedTrackForEvent) {
    auto track = trackAt(12U, 0, 10);
    audition::AcousticEvent event{};

    EXPECT_THROW(
        audition::SpatialIdentityCoordinator::attachToEvent(
            event,
            track),
        audition::Error);
}
