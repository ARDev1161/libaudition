#include <audition/pipeline/acoustic_event_assembler.hpp>

#include <cstdint>
#include <limits>

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
    std::int64_t last_seen,
    std::optional<std::uint64_t> source_id = std::nullopt) {
    audition::SpatialTrack track{};
    track.track_id = audition::SpatialTrackId{track_id};
    if (source_id.has_value()) {
        track.source_id =
            audition::AcousticSourceId{*source_id};
    }
    track.first_seen = ts(first_seen);
    track.last_seen = ts(last_seen);
    return track;
}

audition::PositionEstimate position(
    double x,
    double y,
    double z) {
    audition::PositionEstimate result{};
    result.mean_m = {x, y, z};
    result.covariance_m2 = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0};
    return result;
}

}  // namespace

TEST(AcousticEventAssembler, BeginsUpdatesAndFinishesEvent) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(10));

    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(20);
    patch.track_id = audition::SpatialTrackId{7U};
    patch.source_id = audition::AcousticSourceId{42U};
    patch.range = audition::RangeEstimate{
        {3.0, 0.25},
        audition::Probability::zero(),
        audition::RangeEstimate::Method::Fused};
    patch.position = position(1.0, 2.0, 3.0);

    audition::ClassificationResult classification{};
    classification.classes.push_back(
        {"alarm", audition::Probability::from(0.9)});
    patch.classification = classification;

    audition::Transcript transcript{};
    transcript.segment_id = audition::SpeechSegmentId{5U};
    transcript.text = "help";
    transcript.language = "en";
    patch.transcript = transcript;

    audition::SpeakerIdentity speaker{};
    speaker.speaker_id = audition::SpeakerId{9U};
    speaker.display_name = "operator";
    speaker.similarity = {0.8};
    patch.speaker = speaker;

    audition::AuthenticityResult authenticity{};
    authenticity.bona_fide_score = audition::Score{2.5};
    patch.authenticity = authenticity;

    assembler.update(event_id, patch);

    const auto active = assembler.find(event_id);
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->start_time.nanoseconds(), 10);
    EXPECT_EQ(active->end_time.nanoseconds(), 20);
    ASSERT_TRUE(active->track_id.has_value());
    EXPECT_EQ(*active->track_id, audition::SpatialTrackId{7U});
    ASSERT_TRUE(active->source_id.has_value());
    EXPECT_EQ(*active->source_id, audition::AcousticSourceId{42U});
    ASSERT_TRUE(active->classification.has_value());
    EXPECT_EQ(active->classification->classes[0].label, "alarm");
    ASSERT_TRUE(active->transcript.has_value());
    EXPECT_EQ(active->transcript->text, "help");
    ASSERT_TRUE(active->speaker.has_value());
    EXPECT_EQ(active->speaker->display_name, "operator");
    ASSERT_TRUE(active->authenticity.has_value());

    const auto finished = assembler.finish(event_id, ts(30));
    EXPECT_EQ(finished.event_id, event_id);
    EXPECT_EQ(finished.end_time.nanoseconds(), 30);
    EXPECT_EQ(assembler.activeCount(), 0U);
    EXPECT_FALSE(assembler.find(event_id).has_value());
}

TEST(AcousticEventAssembler, TrackSnapshotUpdatesGeometry) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(0));

    auto track = trackAt(1U, 0, 10, 50U);
    track.direction = {
        audition::Direction3D::fromVector({0.0, 1.0, 0.0}),
        0.01,
        audition::Probability::one()};
    track.range = audition::RangeEstimate{
        {2.0, 0.1},
        audition::Probability::zero(),
        audition::RangeEstimate::Method::LevelPrior};
    track.position = position(0.0, 2.0, 0.0);

    assembler.updateFromTrack(event_id, track);

    const auto event = assembler.find(event_id);
    ASSERT_TRUE(event.has_value());
    ASSERT_TRUE(event->track_id.has_value());
    EXPECT_EQ(*event->track_id, track.track_id);
    ASSERT_TRUE(event->source_id.has_value());
    EXPECT_EQ(*event->source_id, *track.source_id);
    ASSERT_TRUE(event->range.has_value());
    EXPECT_DOUBLE_EQ(event->range->distance_m.mean, 2.0);
    ASSERT_TRUE(event->position.has_value());
    EXPECT_DOUBLE_EQ(event->position->mean_m.y, 2.0);
}

TEST(AcousticEventAssembler, MissingPatchFieldsDoNotEraseKnownGeometry) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(0));

    audition::AcousticEventPatch spatial{};
    spatial.timestamp = ts(10);
    spatial.range = audition::RangeEstimate{
        {4.0, 1.0},
        audition::Probability::zero(),
        audition::RangeEstimate::Method::LevelPrior};
    spatial.position = position(1.0, 0.0, 0.0);
    assembler.update(event_id, spatial);

    audition::AcousticEventPatch semantic{};
    semantic.timestamp = ts(20);
    audition::ClassificationResult classification{};
    classification.classes.push_back(
        {"speech", audition::Probability::from(0.8)});
    semantic.classification = classification;
    assembler.update(event_id, semantic);

    const auto event = assembler.find(event_id);
    ASSERT_TRUE(event.has_value());
    ASSERT_TRUE(event->range.has_value());
    EXPECT_DOUBLE_EQ(event->range->distance_m.mean, 4.0);
    ASSERT_TRUE(event->position.has_value());
    EXPECT_DOUBLE_EQ(event->position->mean_m.x, 1.0);
}

TEST(AcousticEventAssembler, AllowsTrackHandoffForSamePersistentSource) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(0));

    assembler.updateFromTrack(
        event_id,
        trackAt(1U, 0, 10, 77U));
    assembler.updateFromTrack(
        event_id,
        trackAt(2U, 20, 30, 77U));

    const auto event = assembler.find(event_id);
    ASSERT_TRUE(event.has_value());
    ASSERT_TRUE(event->track_id.has_value());
    EXPECT_EQ(*event->track_id, audition::SpatialTrackId{2U});
    ASSERT_TRUE(event->source_id.has_value());
    EXPECT_EQ(*event->source_id, audition::AcousticSourceId{77U});
}

TEST(AcousticEventAssembler, RejectsUnprovenTrackSwitchTransactionally) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(0));

    audition::AcousticEventPatch first{};
    first.timestamp = ts(10);
    first.track_id = audition::SpatialTrackId{1U};
    first.range = audition::RangeEstimate{
        {2.0, 0.1},
        audition::Probability::zero(),
        audition::RangeEstimate::Method::LevelPrior};
    assembler.update(event_id, first);

    audition::AcousticEventPatch conflicting{};
    conflicting.timestamp = ts(20);
    conflicting.track_id = audition::SpatialTrackId{2U};
    conflicting.range = audition::RangeEstimate{
        {99.0, 1.0},
        audition::Probability::zero(),
        audition::RangeEstimate::Method::LevelPrior};

    EXPECT_THROW(
        assembler.update(event_id, conflicting),
        audition::Error);

    const auto event = assembler.find(event_id);
    ASSERT_TRUE(event.has_value());
    ASSERT_TRUE(event->track_id.has_value());
    EXPECT_EQ(*event->track_id, audition::SpatialTrackId{1U});
    ASSERT_TRUE(event->range.has_value());
    EXPECT_DOUBLE_EQ(event->range->distance_m.mean, 2.0);
    EXPECT_EQ(event->end_time.nanoseconds(), 10);
}

TEST(AcousticEventAssembler, RejectsPersistentSourceChangeTransactionally) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(0));

    audition::AcousticEventPatch first{};
    first.timestamp = ts(10);
    first.source_id = audition::AcousticSourceId{1U};
    assembler.update(event_id, first);

    audition::AcousticEventPatch conflicting{};
    conflicting.timestamp = ts(20);
    conflicting.source_id = audition::AcousticSourceId{2U};
    audition::ClassificationResult classification{};
    classification.classes.push_back(
        {"noise", audition::Probability::one()});
    conflicting.classification = classification;

    EXPECT_THROW(
        assembler.update(event_id, conflicting),
        audition::Error);

    const auto event = assembler.find(event_id);
    ASSERT_TRUE(event.has_value());
    ASSERT_TRUE(event->source_id.has_value());
    EXPECT_EQ(*event->source_id, audition::AcousticSourceId{1U});
    EXPECT_FALSE(event->classification.has_value());
    EXPECT_EQ(event->end_time.nanoseconds(), 10);
}

TEST(AcousticEventAssembler, EnforcesClockAndMonotonicUpdateTime) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(100));

    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(110);
    assembler.update(event_id, patch);

    patch.timestamp = ts(109);
    EXPECT_THROW(
        assembler.update(event_id, patch),
        audition::Error);

    patch.timestamp = ts(120, 9U);
    try {
        assembler.update(event_id, patch);
        FAIL() << "Expected clock mismatch";
    } catch (const audition::Error& error) {
        EXPECT_EQ(
            error.code(),
            audition::ErrorCode::ClockDomainMismatch);
    }
}

TEST(AcousticEventAssembler, FinishCannotMoveEndBackwardOrAcrossClocks) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(100));

    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(150);
    assembler.update(event_id, patch);

    EXPECT_THROW(
        static_cast<void>(assembler.finish(event_id, ts(149))),
        audition::Error);
    EXPECT_TRUE(assembler.find(event_id).has_value());

    try {
        static_cast<void>(
            assembler.finish(event_id, ts(160, 3U)));
        FAIL() << "Expected clock mismatch";
    } catch (const audition::Error& error) {
        EXPECT_EQ(
            error.code(),
            audition::ErrorCode::ClockDomainMismatch);
    }

    EXPECT_TRUE(assembler.find(event_id).has_value());
}

TEST(AcousticEventAssembler, SupportsOverlappingEventsDeterministically) {
    audition::AcousticEventAssembler assembler;

    const auto first = assembler.begin(ts(0));
    const auto second = assembler.begin(ts(5));
    const auto third = assembler.begin(ts(10));

    const auto active = assembler.activeEvents();
    ASSERT_EQ(active.size(), 3U);
    EXPECT_EQ(active[0].event_id, first);
    EXPECT_EQ(active[1].event_id, second);
    EXPECT_EQ(active[2].event_id, third);
}

TEST(AcousticEventAssembler, CapacityCancelAndResetAreExplicit) {
    audition::AcousticEventAssemblerOptions options{};
    options.max_active_events = 2U;
    audition::AcousticEventAssembler assembler{options};

    const auto first = assembler.begin(ts(0));
    const auto second = assembler.begin(ts(1));
    EXPECT_THROW(
        static_cast<void>(assembler.begin(ts(2))),
        audition::Error);

    assembler.cancel(first);
    const auto third = assembler.begin(ts(3));
    EXPECT_NE(third, first);
    EXPECT_EQ(assembler.activeCount(), 2U);

    assembler.reset();
    EXPECT_EQ(assembler.activeCount(), 0U);

    const auto fourth = assembler.begin(ts(4));
    EXPECT_NE(fourth, first);
    EXPECT_NE(fourth, second);
    EXPECT_NE(fourth, third);
}

TEST(AcousticEventAssembler, RejectsInvalidChildIdentityAnnotations) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(0));

    audition::AcousticEventPatch transcript_patch{};
    transcript_patch.timestamp = ts(1);
    audition::Transcript transcript{};
    transcript.text = "invalid without segment id";
    transcript_patch.transcript = transcript;

    EXPECT_THROW(
        assembler.update(event_id, transcript_patch),
        audition::Error);

    audition::AcousticEventPatch speaker_patch{};
    speaker_patch.timestamp = ts(1);
    audition::SpeakerIdentity speaker{};
    speaker.display_name = "invalid";
    speaker_patch.speaker = speaker;

    EXPECT_THROW(
        assembler.update(event_id, speaker_patch),
        audition::Error);
}

TEST(AcousticEventAssembler, RejectsInvalidMetricGeometry) {
    audition::AcousticEventAssembler assembler;
    const auto event_id = assembler.begin(ts(0));

    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(1);
    patch.range = audition::RangeEstimate{
        {-1.0, 1.0},
        audition::Probability::zero(),
        audition::RangeEstimate::Method::Unknown};

    EXPECT_THROW(
        assembler.update(event_id, patch),
        audition::Error);

    patch.range.reset();
    auto invalid_position = position(0.0, 0.0, 0.0);
    invalid_position.covariance_m2[0] =
        std::numeric_limits<double>::infinity();
    patch.position = invalid_position;

    EXPECT_THROW(
        assembler.update(event_id, patch),
        audition::Error);
}

TEST(AcousticEventAssembler, UnknownOrInvalidIdsAreRejected) {
    audition::AcousticEventAssembler assembler;

    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(0);

    EXPECT_THROW(
        assembler.update(audition::AcousticEventId{}, patch),
        audition::Error);
    EXPECT_THROW(
        assembler.update(audition::AcousticEventId{99U}, patch),
        audition::Error);
    EXPECT_THROW(
        static_cast<void>(
            assembler.finish(
                audition::AcousticEventId{99U},
                ts(0))),
        audition::Error);
}

TEST(AcousticEventAssembler, RejectsZeroCapacity) {
    audition::AcousticEventAssemblerOptions options{};
    options.max_active_events = 0U;

    EXPECT_THROW(
        audition::AcousticEventAssembler{options},
        audition::Error);
}


TEST(AcousticEventAssembler, ConflictingUpdateDoesNotMutateOtherActiveEvent) {
    audition::AcousticEventAssembler assembler;
    const auto first = assembler.begin(ts(0));
    const auto second = assembler.begin(ts(0));

    audition::AcousticEventPatch first_patch{};
    first_patch.timestamp = ts(10);
    first_patch.track_id = audition::SpatialTrackId{1U};
    first_patch.source_id = audition::AcousticSourceId{101U};
    assembler.update(first, first_patch);

    audition::AcousticEventPatch second_patch{};
    second_patch.timestamp = ts(12);
    second_patch.track_id = audition::SpatialTrackId{2U};
    second_patch.source_id = audition::AcousticSourceId{202U};
    assembler.update(second, second_patch);

    auto conflict = first_patch;
    conflict.timestamp = ts(20);
    conflict.track_id = audition::SpatialTrackId{2U};
    conflict.source_id = audition::AcousticSourceId{202U};
    EXPECT_THROW(assembler.update(first, conflict), audition::Error);

    const auto unchanged = assembler.find(first);
    ASSERT_TRUE(unchanged.has_value());
    ASSERT_TRUE(unchanged->track_id.has_value());
    ASSERT_TRUE(unchanged->source_id.has_value());
    EXPECT_EQ(*unchanged->track_id, audition::SpatialTrackId{1U});
    EXPECT_EQ(*unchanged->source_id, audition::AcousticSourceId{101U});
    EXPECT_EQ(unchanged->end_time.nanoseconds(), 10);

    auto valid = second_patch;
    valid.timestamp = ts(25);
    audition::ClassificationResult classification{};
    classification.classes.push_back(
        {"speech", audition::Probability::from(0.9)});
    valid.classification = classification;
    EXPECT_NO_THROW(assembler.update(second, valid));
    const auto healthy = assembler.find(second);
    ASSERT_TRUE(healthy.has_value());
    ASSERT_TRUE(healthy->classification.has_value());
    EXPECT_EQ(healthy->classification->classes[0].label, "speech");
    EXPECT_EQ(healthy->end_time.nanoseconds(), 25);
    EXPECT_EQ(assembler.activeCount(), 2U);
}


TEST(AcousticEventAssembler, InvalidTimestampPatchIsAtomicAndLaterValidUpdateSucceeds) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(100));

    audition::AcousticEventPatch first{};
    first.timestamp = ts(120);
    first.track_id = audition::SpatialTrackId{10U};
    first.source_id = audition::AcousticSourceId{20U};
    first.position = position(1.0, 2.0, 3.0);
    assembler.update(id, first);

    audition::AcousticEventPatch invalid{};
    invalid.timestamp = ts(119);
    invalid.position = position(99.0, 99.0, 99.0);
    EXPECT_THROW(assembler.update(id, invalid), audition::Error);
    invalid.timestamp = ts(130, 2U);
    EXPECT_THROW(assembler.update(id, invalid), audition::Error);

    const auto unchanged = assembler.find(id);
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_EQ(unchanged->end_time.nanoseconds(), 120);
    ASSERT_TRUE(unchanged->position.has_value());
    EXPECT_DOUBLE_EQ(unchanged->position->mean_m.x, 1.0);
    EXPECT_EQ(unchanged->source_id, first.source_id);
    EXPECT_EQ(unchanged->track_id, first.track_id);

    audition::AcousticEventPatch valid{};
    valid.timestamp = ts(140);
    valid.position = position(4.0, 5.0, 6.0);
    EXPECT_NO_THROW(assembler.update(id, valid));
    const auto complete = assembler.finish(id, ts(150));
    EXPECT_EQ(complete.end_time.nanoseconds(), 150);
    ASSERT_TRUE(complete.position.has_value());
    EXPECT_DOUBLE_EQ(complete.position->mean_m.x, 4.0);
    EXPECT_EQ(complete.source_id, first.source_id);
}
