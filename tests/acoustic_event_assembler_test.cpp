#include <audition/pipeline/acoustic_event_assembler.hpp>

#include <cstdint>
#include <limits>
#include <utility>

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


TEST(AcousticEventAssembler, FailedFinishKeepsEventAvailableForRecovery) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(100));
    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(150);
    patch.source_id = audition::AcousticSourceId{17U};
    assembler.update(id, patch);

    EXPECT_THROW(static_cast<void>(assembler.finish(id, ts(149))), audition::Error);
    EXPECT_THROW(static_cast<void>(assembler.finish(id, ts(160, 9U))), audition::Error);
    EXPECT_EQ(assembler.activeCount(), 1U);

    const auto pending = assembler.find(id);
    ASSERT_TRUE(pending.has_value());
    EXPECT_EQ(pending->end_time.nanoseconds(), 150);
    EXPECT_EQ(pending->source_id, patch.source_id);

    const auto completed = assembler.finish(id, ts(150));
    EXPECT_EQ(completed.end_time.nanoseconds(), 150);
    EXPECT_EQ(completed.source_id, patch.source_id);
    EXPECT_EQ(assembler.activeCount(), 0U);
    EXPECT_FALSE(assembler.find(id).has_value());
    EXPECT_THROW(static_cast<void>(assembler.finish(id, ts(200))), audition::Error);
}


TEST(AcousticEventAssembler, FailedTrackHandoffDoesNotOverwriteExistingGeometry) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    auto original = trackAt(1U, 0, 10, 101U);
    original.position = position(1.0, 2.0, 3.0);
    assembler.updateFromTrack(id, original);

    auto conflict = trackAt(2U, 10, 20, 202U);
    conflict.position = position(99.0, 99.0, 99.0);
    EXPECT_THROW(assembler.updateFromTrack(id, conflict), audition::Error);

    const auto snapshot = assembler.find(id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->track_id, original.track_id);
    EXPECT_EQ(snapshot->source_id, original.source_id);
    EXPECT_EQ(snapshot->end_time.nanoseconds(), 10);
    ASSERT_TRUE(snapshot->position.has_value());
    EXPECT_DOUBLE_EQ(snapshot->position->mean_m.x, 1.0);

    auto handoff = trackAt(2U, 10, 30, 101U);
    handoff.position = position(4.0, 5.0, 6.0);
    EXPECT_NO_THROW(assembler.updateFromTrack(id, handoff));
    const auto updated = assembler.find(id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->track_id, handoff.track_id);
    EXPECT_EQ(updated->source_id, original.source_id);
    EXPECT_EQ(updated->end_time.nanoseconds(), 30);
    ASSERT_TRUE(updated->position.has_value());
    EXPECT_DOUBLE_EQ(updated->position->mean_m.x, 4.0);
}


TEST(AcousticEventAssembler, TrackHandoffPreservesAnnotationsWhenSnapshotOmitsThem) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));

    auto original = trackAt(11U, 0, 10, 777U);
    original.position = position(1.0, 2.0, 3.0);
    original.range = audition::RangeEstimate{
        {2.0, 0.1},
        audition::Probability::zero(),
        audition::RangeEstimate::Method::LevelPrior};
    assembler.updateFromTrack(id, original);

    audition::AcousticEventPatch speech{};
    speech.timestamp = ts(15);
    audition::Transcript transcript{};
    transcript.segment_id = audition::SpeechSegmentId{55U};
    transcript.text = "stop";
    speech.transcript = transcript;
    assembler.update(id, speech);

    auto successor = trackAt(12U, 10, 20, 777U);
    assembler.updateFromTrack(id, successor);

    const auto event = assembler.find(id);
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->track_id, successor.track_id);
    EXPECT_EQ(event->source_id, original.source_id);
    EXPECT_EQ(event->end_time.nanoseconds(), 20);
    ASSERT_TRUE(event->position.has_value());
    EXPECT_DOUBLE_EQ(event->position->mean_m.x, 1.0);
    ASSERT_TRUE(event->range.has_value());
    EXPECT_DOUBLE_EQ(event->range->distance_m.mean, 2.0);
    ASSERT_TRUE(event->transcript.has_value());
    EXPECT_EQ(event->transcript->text, "stop");
}


TEST(AcousticEventAssembler, RejectedCrossSourceHandoffRetainsSpeechAnnotations) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    auto first = trackAt(10U, 0, 10, 100U);
    first.position = position(1.0, 0.0, 0.0);
    assembler.updateFromTrack(id, first);

    audition::AcousticEventPatch speech{};
    speech.timestamp = ts(15);
    audition::Transcript transcript{};
    transcript.segment_id = audition::SpeechSegmentId{9U};
    transcript.text = "original";
    speech.transcript = transcript;
    assembler.update(id, speech);

    auto impostor = trackAt(20U, 10, 30, 200U);
    impostor.position = position(20.0, 0.0, 0.0);
    EXPECT_THROW(assembler.updateFromTrack(id, impostor), audition::Error);

    const auto unchanged = assembler.find(id);
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_EQ(unchanged->track_id, first.track_id);
    EXPECT_EQ(unchanged->source_id, first.source_id);
    EXPECT_EQ(unchanged->end_time.nanoseconds(), 15);
    ASSERT_TRUE(unchanged->transcript.has_value());
    EXPECT_EQ(unchanged->transcript->text, "original");
    ASSERT_TRUE(unchanged->position.has_value());
    EXPECT_DOUBLE_EQ(unchanged->position->mean_m.x, 1.0);

    auto valid = trackAt(21U, 15, 40, 100U);
    assembler.updateFromTrack(id, valid);
    const auto recovered = assembler.finish(id, ts(45));
    EXPECT_EQ(recovered.track_id, valid.track_id);
    EXPECT_EQ(recovered.source_id, first.source_id);
    ASSERT_TRUE(recovered.transcript.has_value());
    EXPECT_EQ(recovered.transcript->text, "original");
}


TEST(AcousticEventAssembler, CancelIsIdempotentAndDoesNotDisturbConcurrentEvent) {
    audition::AcousticEventAssembler assembler;
    const auto cancelled = assembler.begin(ts(0));
    const auto retained = assembler.begin(ts(5));

    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(10);
    patch.source_id = audition::AcousticSourceId{88U};
    assembler.update(retained, patch);

    assembler.cancel(cancelled);
    assembler.cancel(cancelled);
    assembler.cancel(audition::AcousticEventId{9999U});
    EXPECT_FALSE(assembler.find(cancelled).has_value());
    EXPECT_EQ(assembler.activeCount(), 1U);

    const auto remaining = assembler.find(retained);
    ASSERT_TRUE(remaining.has_value());
    EXPECT_EQ(remaining->source_id, patch.source_id);
    EXPECT_EQ(remaining->end_time.nanoseconds(), 10);
    const auto result = assembler.finish(retained, ts(20));
    EXPECT_EQ(result.source_id, patch.source_id);
    EXPECT_EQ(assembler.activeCount(), 0U);
}


TEST(AcousticEventAssembler, EmptyEventCanFinishAtStartTimestamp) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(42));
    const auto event = assembler.finish(id, ts(42));
    EXPECT_EQ(event.event_id, id);
    EXPECT_EQ(event.start_time.nanoseconds(), 42);
    EXPECT_EQ(event.end_time.nanoseconds(), 42);
    EXPECT_FALSE(event.track_id.has_value());
    EXPECT_FALSE(event.source_id.has_value());
    EXPECT_FALSE(assembler.find(id).has_value());
}

TEST(AcousticEventAssembler, EventIdsRemainMonotonicAcrossResetAndCancel) {
    audition::AcousticEventAssembler assembler;
    const auto first = assembler.begin(ts(0));
    assembler.cancel(first);
    const auto second = assembler.begin(ts(0));
    assembler.reset();
    const auto third = assembler.begin(ts(0));
    EXPECT_LT(first.value(), second.value());
    EXPECT_LT(second.value(), third.value());
    EXPECT_EQ(assembler.activeCount(), 1U);
}

TEST(AcousticEventAssembler, FailedCapacityAllocationDoesNotSkipEventId) {
    audition::AcousticEventAssembler assembler{{1U}};
    const auto first = assembler.begin(ts(0));
    EXPECT_THROW(static_cast<void>(assembler.begin(ts(1))), audition::Error);
    assembler.cancel(first);
    const auto next = assembler.begin(ts(2));
    EXPECT_EQ(next.value(), first.value() + 1U);
}

TEST(AcousticEventAssembler, MultipleEventClocksAreIndependent) {
    audition::AcousticEventAssembler assembler;
    const auto first = assembler.begin(ts(0, 1U));
    const auto second = assembler.begin(ts(100, 2U));
    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(30, 1U);
    patch.track_id = audition::SpatialTrackId{3U};
    EXPECT_NO_THROW(assembler.update(first, patch));
    patch.timestamp = ts(120, 2U);
    patch.track_id = audition::SpatialTrackId{4U};
    EXPECT_NO_THROW(assembler.update(second, patch));
    EXPECT_EQ(assembler.finish(first, ts(40, 1U)).end_time.nanoseconds(), 40);
    EXPECT_EQ(assembler.finish(second, ts(130, 2U)).end_time.nanoseconds(), 130);
}

TEST(AcousticEventAssembler, RepeatedEqualTimestampPatchesPreserveExistingFields) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(10));
    audition::AcousticEventPatch identity{};
    identity.timestamp = ts(20);
    identity.track_id = audition::SpatialTrackId{1U};
    identity.source_id = audition::AcousticSourceId{11U};
    assembler.update(id, identity);
    audition::AcousticEventPatch classification{};
    classification.timestamp = ts(20);
    audition::ClassificationResult result{};
    result.classes.push_back({"alarm", audition::Probability::from(0.8)});
    classification.classification = result;
    assembler.update(id, classification);
    const auto snapshot = assembler.find(id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->end_time.nanoseconds(), 20);
    ASSERT_TRUE(snapshot->track_id.has_value());
    EXPECT_EQ(snapshot->track_id->value(), 1U);
    ASSERT_TRUE(snapshot->classification.has_value());
    EXPECT_EQ(snapshot->classification->classes.front().label, "alarm");
}

TEST(AcousticEventAssembler, RejectsNonFiniteAndNegativeDirectionVariance) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(1);
    patch.direction = audition::DirectionEstimate{};
    for (const double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
        patch.direction->angular_variance_rad2 = invalid;
        EXPECT_THROW(assembler.update(id, patch), audition::Error);
        EXPECT_EQ(assembler.find(id)->end_time.nanoseconds(), 0);
    }
    patch.direction->angular_variance_rad2 = 0.0;
    EXPECT_NO_THROW(assembler.update(id, patch));
    EXPECT_EQ(assembler.find(id)->end_time.nanoseconds(), 1);
}

TEST(AcousticEventAssembler, RejectsEachNonFinitePositionCoordinate) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(10);
    const double bad = std::numeric_limits<double>::quiet_NaN();
    for (int axis = 0; axis < 3; ++axis) {
        patch.position = position(1.0, 2.0, 3.0);
        if (axis == 0) patch.position->mean_m.x = bad;
        if (axis == 1) patch.position->mean_m.y = bad;
        if (axis == 2) patch.position->mean_m.z = bad;
        EXPECT_THROW(assembler.update(id, patch), audition::Error);
    }
    EXPECT_EQ(assembler.find(id)->end_time.nanoseconds(), 0);
    patch.position = position(1.0, 2.0, 3.0);
    EXPECT_NO_THROW(assembler.update(id, patch));
}

TEST(AcousticEventAssembler, RejectsNonFinitePositionCovarianceWithoutCommit) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(10);
    patch.track_id = audition::SpatialTrackId{5U};
    patch.position = position(1.0, 2.0, 3.0);
    patch.position->covariance_m2[4] =
        std::numeric_limits<double>::infinity();
    EXPECT_THROW(assembler.update(id, patch), audition::Error);
    const auto before = assembler.find(id);
    ASSERT_TRUE(before.has_value());
    EXPECT_FALSE(before->track_id.has_value());
    EXPECT_FALSE(before->position.has_value());
    EXPECT_EQ(before->end_time.nanoseconds(), 0);
    patch.position->covariance_m2[4] = 1.0;
    EXPECT_NO_THROW(assembler.update(id, patch));
    EXPECT_EQ(assembler.find(id)->track_id->value(), 5U);
}

TEST(AcousticEventAssembler, PatchFromTrackRejectsInvalidLifecycles) {
    auto missing_id = trackAt(0U, 0, 10);
    EXPECT_THROW(static_cast<void>(
        audition::AcousticEventAssembler::patchFromTrack(missing_id)), audition::Error);
    auto clock_change = trackAt(7U, 0, 10);
    clock_change.last_seen = ts(10, 3U);
    EXPECT_THROW(static_cast<void>(
        audition::AcousticEventAssembler::patchFromTrack(clock_change)), audition::Error);
    auto backwards = trackAt(7U, 10, 9);
    EXPECT_THROW(static_cast<void>(
        audition::AcousticEventAssembler::patchFromTrack(backwards)), audition::Error);
    auto bad_source = trackAt(7U, 0, 10, 0U);
    EXPECT_THROW(static_cast<void>(
        audition::AcousticEventAssembler::patchFromTrack(bad_source)), audition::Error);
}

TEST(AcousticEventAssembler, FailedTrackSnapshotValidationDoesNotChangeEvent) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    auto valid = trackAt(9U, 0, 1, 10U);
    valid.position = position(1.0, 2.0, 3.0);
    assembler.updateFromTrack(id, valid);
    auto invalid = trackAt(9U, 0, 2, 10U);
    invalid.range = audition::RangeEstimate{
        {-1.0, 1.0}, audition::Probability::one(),
        audition::RangeEstimate::Method::LevelPrior};
    EXPECT_THROW(assembler.updateFromTrack(id, invalid), audition::Error);
    const auto stored = assembler.find(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->end_time.nanoseconds(), 1);
    ASSERT_TRUE(stored->position.has_value());
    EXPECT_DOUBLE_EQ(stored->position->mean_m.x, 1.0);
}

TEST(AcousticEventAssembler, ActiveEventsReturnsValueSnapshotsNotLiveReferences) {
    audition::AcousticEventAssembler assembler;
    const auto first = assembler.begin(ts(0));
    const auto second = assembler.begin(ts(0));
    auto snapshot = assembler.activeEvents();
    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[0].event_id, first);
    EXPECT_EQ(snapshot[1].event_id, second);
    snapshot[0].source_id = audition::AcousticSourceId{888U};
    EXPECT_FALSE(assembler.find(first)->source_id.has_value());
    assembler.cancel(first);
    EXPECT_EQ(snapshot.size(), 2U);
    ASSERT_EQ(assembler.activeEvents().size(), 1U);
    EXPECT_EQ(assembler.activeEvents().front().event_id, second);
}


TEST(AcousticEventAssembler, RejectsInvalidPatchTrackWithoutAdvancingEvent) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(10));
    audition::AcousticEventPatch bad{};
    bad.timestamp = ts(20);
    bad.track_id = audition::SpatialTrackId{0U};
    bad.source_id = audition::AcousticSourceId{5U};
    EXPECT_THROW(assembler.update(id, bad), audition::Error);
    const auto current = assembler.find(id);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->end_time.nanoseconds(), 10);
    EXPECT_FALSE(current->track_id.has_value());
    EXPECT_FALSE(current->source_id.has_value());
}

TEST(AcousticEventAssembler, RejectsInvalidPatchSourceWithoutAdvancingEvent) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(10));
    audition::AcousticEventPatch bad{};
    bad.timestamp = ts(20);
    bad.track_id = audition::SpatialTrackId{1U};
    bad.source_id = audition::AcousticSourceId{0U};
    EXPECT_THROW(assembler.update(id, bad), audition::Error);
    const auto current = assembler.find(id);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->end_time.nanoseconds(), 10);
    EXPECT_FALSE(current->track_id.has_value());
}

TEST(AcousticEventAssembler, RejectsInvalidTranscriptBeforeUpdatingClassification) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch bad{};
    bad.timestamp = ts(10);
    audition::Transcript transcript{};
    transcript.text = "unattributed";
    bad.transcript = transcript;
    audition::ClassificationResult classes{};
    classes.classes.push_back({"Speech", audition::Probability::one()});
    bad.classification = classes;
    EXPECT_THROW(assembler.update(id, bad), audition::Error);
    const auto current = assembler.find(id);
    ASSERT_TRUE(current.has_value());
    EXPECT_FALSE(current->classification.has_value());
    EXPECT_FALSE(current->transcript.has_value());
    EXPECT_EQ(current->end_time.nanoseconds(), 0);
}

TEST(AcousticEventAssembler, RejectsInvalidSpeakerBeforeUpdatingTranscript) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch bad{};
    bad.timestamp = ts(10);
    audition::SpeakerIdentity speaker{};
    bad.speaker = speaker;
    audition::Transcript transcript{};
    transcript.segment_id = audition::SpeechSegmentId{1U};
    transcript.text = "hello";
    bad.transcript = transcript;
    EXPECT_THROW(assembler.update(id, bad), audition::Error);
    const auto current = assembler.find(id);
    ASSERT_TRUE(current.has_value());
    EXPECT_FALSE(current->transcript.has_value());
    EXPECT_FALSE(current->speaker.has_value());
}

TEST(AcousticEventAssembler, RejectsInvalidRangeVarianceAndNonfiniteMean) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(10);
    for (const auto& [mean, variance] :
         {std::pair<double, double>{1.0, -1.0},
          {std::numeric_limits<double>::infinity(), 1.0},
          {1.0, std::numeric_limits<double>::quiet_NaN()}}) {
        patch.range = audition::RangeEstimate{
            {mean, variance}, audition::Probability::one(),
            audition::RangeEstimate::Method::LevelPrior};
        EXPECT_THROW(assembler.update(id, patch), audition::Error);
    }
    EXPECT_FALSE(assembler.find(id)->range.has_value());
    EXPECT_EQ(assembler.find(id)->end_time.nanoseconds(), 0);
}

TEST(AcousticEventAssembler, DirectionWithoutVarianceIsAccepted) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch patch{};
    patch.timestamp = ts(1);
    patch.direction = audition::DirectionEstimate{};
    patch.direction->direction =
        audition::Direction3D::fromVector({0.0, 1.0, 0.0});
    EXPECT_NO_THROW(assembler.update(id, patch));
    const auto result = assembler.find(id);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->direction.has_value());
    EXPECT_FALSE(result->direction->angular_variance_rad2.has_value());
}

TEST(AcousticEventAssembler, ExistingTrackCanGainPersistentSourceLater) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch first{};
    first.timestamp = ts(10);
    first.track_id = audition::SpatialTrackId{11U};
    assembler.update(id, first);
    audition::AcousticEventPatch second{};
    second.timestamp = ts(20);
    second.track_id = first.track_id;
    second.source_id = audition::AcousticSourceId{77U};
    EXPECT_NO_THROW(assembler.update(id, second));
    const auto snapshot = assembler.find(id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->track_id, first.track_id);
    EXPECT_EQ(snapshot->source_id, second.source_id);
}

TEST(AcousticEventAssembler, TrackSwitchWithImplicitSourceIsRejected) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch first{};
    first.timestamp = ts(10);
    first.track_id = audition::SpatialTrackId{1U};
    first.source_id = audition::AcousticSourceId{77U};
    assembler.update(id, first);
    audition::AcousticEventPatch handoff{};
    handoff.timestamp = ts(20);
    handoff.track_id = audition::SpatialTrackId{2U};
    // Even an existing persistent ID is insufficient: the update must affirm it.
    EXPECT_THROW(assembler.update(id, handoff), audition::Error);
    EXPECT_EQ(assembler.find(id)->track_id, first.track_id);
    EXPECT_EQ(assembler.find(id)->source_id, first.source_id);
    EXPECT_EQ(assembler.find(id)->end_time.nanoseconds(), 10);
}

TEST(AcousticEventAssembler, ExplicitSourceAuthorizesSameTimestampHandoff) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch first{};
    first.timestamp = ts(10);
    first.track_id = audition::SpatialTrackId{1U};
    first.source_id = audition::AcousticSourceId{77U};
    assembler.update(id, first);
    audition::AcousticEventPatch handoff{};
    handoff.timestamp = ts(10);
    handoff.track_id = audition::SpatialTrackId{2U};
    handoff.source_id = first.source_id;
    EXPECT_NO_THROW(assembler.update(id, handoff));
    const auto snapshot = assembler.find(id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->track_id, handoff.track_id);
    EXPECT_EQ(snapshot->source_id, first.source_id);
    EXPECT_EQ(snapshot->end_time.nanoseconds(), 10);
}

TEST(AcousticEventAssembler, IndependentSpeechAndVoicePatchesAccumulate) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch speech{};
    speech.timestamp = ts(10);
    audition::Transcript transcript{};
    transcript.segment_id = audition::SpeechSegmentId{33U};
    transcript.text = "stop";
    speech.transcript = transcript;
    assembler.update(id, speech);

    audition::AcousticEventPatch voice{};
    voice.timestamp = ts(20);
    voice.voice_traits = audition::VoiceTraits{};
    voice.voice_state = audition::VoiceState{};
    voice.authenticity = audition::AuthenticityResult{};
    assembler.update(id, voice);
    const auto snapshot = assembler.find(id);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_TRUE(snapshot->transcript.has_value());
    EXPECT_EQ(snapshot->transcript->text, "stop");
    EXPECT_TRUE(snapshot->voice_traits.has_value());
    EXPECT_TRUE(snapshot->voice_state.has_value());
    EXPECT_TRUE(snapshot->authenticity.has_value());
}

TEST(AcousticEventAssembler, PatchFromTrackDoesNotIntroduceSemanticAnnotations) {
    auto track = trackAt(14U, 5, 10, 80U);
    track.direction.direction =
        audition::Direction3D::fromVector({0.0, 0.0, 1.0});
    track.fingerprint = audition::SourceFingerprint{};
    const auto patch = audition::AcousticEventAssembler::patchFromTrack(track);
    EXPECT_EQ(patch.timestamp.nanoseconds(), 10);
    EXPECT_EQ(patch.track_id, track.track_id);
    EXPECT_EQ(patch.source_id, track.source_id);
    EXPECT_FALSE(patch.transcript.has_value());
    EXPECT_FALSE(patch.speaker.has_value());
    EXPECT_FALSE(patch.classification.has_value());
    EXPECT_FALSE(patch.voice_traits.has_value());
    EXPECT_FALSE(patch.voice_state.has_value());
}

TEST(AcousticEventAssembler, EmptyAnnotationMayAdvanceTimeButNotClearGeometry) {
    audition::AcousticEventAssembler assembler;
    const auto id = assembler.begin(ts(0));
    audition::AcousticEventPatch first{};
    first.timestamp = ts(10);
    first.position = position(1.0, 2.0, 3.0);
    first.source_id = audition::AcousticSourceId{91U};
    assembler.update(id, first);
    audition::AcousticEventPatch empty{};
    empty.timestamp = ts(20);
    assembler.update(id, empty);
    const auto current = assembler.find(id);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->end_time.nanoseconds(), 20);
    ASSERT_TRUE(current->position.has_value());
    EXPECT_DOUBLE_EQ(current->position->mean_m.x, 1.0);
    EXPECT_EQ(current->source_id, first.source_id);
}
