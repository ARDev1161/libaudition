#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/source_identity_resolver.hpp>
#include <audition/pipeline/acoustic_event_assembler.hpp>
#include <audition/pipeline/spatial_identity.hpp>

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

namespace {

audition::Timestamp ts(std::int64_t nanoseconds) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, 0U}};
}

class NullFusion final : public audition::ISpatialFusion {
public:
    audition::BackendInfo backendInfo() const override {
        return {"scenario-null-fusion", "1"};
    }

    std::optional<audition::PositionEstimate> fuse(
        const audition::SpatialFusionInput&) const override {
        return std::nullopt;
    }
};

audition::SourceFingerprint fingerprint(
    float x,
    float y) {
    audition::SourceFingerprint result{};
    result.model_id = "scenario-acoustic-fingerprint-v1";
    result.embedding = {x, y};
    result.quality = audition::Probability::one();
    return result;
}

audition::SpatialTrack track(
    std::uint64_t track_id,
    std::int64_t first_seen,
    std::int64_t last_seen,
    audition::SourceFingerprint source_fingerprint) {
    audition::SpatialTrack result{};
    result.track_id = audition::SpatialTrackId{track_id};
    result.first_seen = ts(first_seen);
    result.last_seen = ts(last_seen);
    result.direction = {
        audition::Direction3D::fromVector({1.0, 0.0, 0.0}),
        0.02,
        audition::Probability::one()};
    result.fingerprint = std::move(source_fingerprint);
    return result;
}

}  // namespace

TEST(AcousticPipelineScenario, PersistentSourceSurvivesTrackerHandoffInsideEvent) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    NullFusion fusion;
    audition::SpatialIdentityCoordinator identity{
        fusion,
        resolver};
    audition::AcousticEventAssembler events;

    auto first_track = track(
        11U,
        0,
        10,
        fingerprint(1.0F, 0.0F));

    const auto first_identity =
        identity.observe(first_track);
    ASSERT_TRUE(first_identity.newly_created);
    ASSERT_TRUE(first_track.source_id.has_value());

    const auto event_id = events.begin(ts(0));
    events.updateFromTrack(event_id, first_track);

    audition::AcousticEventPatch classification_patch{};
    classification_patch.timestamp = ts(15);
    audition::ClassificationResult classification{};
    classification.classes.push_back(
        {"alarm", audition::Probability::from(0.95)});
    classification_patch.classification =
        classification;
    events.update(event_id, classification_patch);

    identity.endTrack(first_track.track_id, ts(20));

    auto replacement_track = track(
        37U,
        25,
        30,
        fingerprint(0.999F, 0.01F));

    const auto replacement_identity =
        identity.observe(replacement_track);
    ASSERT_FALSE(replacement_identity.newly_created);
    ASSERT_TRUE(replacement_track.source_id.has_value());
    EXPECT_EQ(
        *replacement_track.source_id,
        *first_track.source_id);

    // AcousticEventAssembler accepts this transient track switch because the
    // persistent source identity proves continuity.
    events.updateFromTrack(event_id, replacement_track);

    audition::AcousticEventPatch transcript_patch{};
    transcript_patch.timestamp = ts(35);
    audition::Transcript transcript{};
    transcript.segment_id = audition::SpeechSegmentId{8U};
    transcript.text = "help";
    transcript.language = "en";
    transcript.confidence =
        audition::Probability::from(0.9);
    transcript_patch.transcript = transcript;
    events.update(event_id, transcript_patch);

    const audition::AcousticEvent completed =
        events.finish(event_id, ts(40));

    EXPECT_EQ(completed.start_time.nanoseconds(), 0);
    EXPECT_EQ(completed.end_time.nanoseconds(), 40);

    ASSERT_TRUE(completed.track_id.has_value());
    EXPECT_EQ(
        *completed.track_id,
        replacement_track.track_id);

    ASSERT_TRUE(completed.source_id.has_value());
    EXPECT_EQ(
        *completed.source_id,
        *first_track.source_id);

    ASSERT_TRUE(completed.classification.has_value());
    ASSERT_EQ(
        completed.classification->classes.size(),
        1U);
    EXPECT_EQ(
        completed.classification->classes[0].label,
        "alarm");

    ASSERT_TRUE(completed.transcript.has_value());
    EXPECT_EQ(completed.transcript->text, "help");

    EXPECT_EQ(events.activeCount(), 0U);
    EXPECT_EQ(registry.list().size(), 1U);
}

TEST(AcousticPipelineScenario, SeparateActiveSourcesRemainSeparateEvents) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    NullFusion fusion;
    audition::SpatialIdentityCoordinator identity{
        fusion,
        resolver};
    audition::AcousticEventAssembler events;

    auto left = track(
        1U,
        0,
        10,
        fingerprint(1.0F, 0.0F));
    auto right = track(
        2U,
        0,
        11,
        fingerprint(0.0F, 1.0F));

    static_cast<void>(identity.observe(left));
    static_cast<void>(identity.observe(right));

    ASSERT_TRUE(left.source_id.has_value());
    ASSERT_TRUE(right.source_id.has_value());
    EXPECT_NE(*left.source_id, *right.source_id);

    const auto left_event = events.begin(ts(0));
    const auto right_event = events.begin(ts(0));
    events.updateFromTrack(left_event, left);
    events.updateFromTrack(right_event, right);

    const auto active = events.activeEvents();
    ASSERT_EQ(active.size(), 2U);
    ASSERT_TRUE(active[0].source_id.has_value());
    ASSERT_TRUE(active[1].source_id.has_value());
    EXPECT_NE(
        *active[0].source_id,
        *active[1].source_id);
}
