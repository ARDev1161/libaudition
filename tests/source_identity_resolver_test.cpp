#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/source_identity_resolver.hpp>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
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

audition::SourceFingerprint fingerprint(
    std::string model_id,
    std::initializer_list<float> values,
    double quality = 1.0) {
    audition::SourceFingerprint result{};
    result.model_id = std::move(model_id);
    result.embedding.assign(values.begin(), values.end());
    result.quality = audition::Probability::from(quality);
    return result;
}

audition::SourceIdentityObservation observation(
    std::uint64_t track_id,
    std::int64_t nanoseconds) {
    audition::SourceIdentityObservation result{};
    result.track_id = audition::SpatialTrackId{track_id};
    result.timestamp = ts(nanoseconds);
    return result;
}

audition::PositionEstimate position(double x, double y, double z) {
    audition::PositionEstimate result{};
    result.mean_m = {x, y, z};
    return result;
}

// Inject registry faults to exercise the resolver's recovery after
// persistent state APIs reject an observation.
class FaultingRegistry final : public audition::IAcousticSourceRegistry {
public:
    audition::AcousticSourceId create() override {
        if (fail_create) throw std::runtime_error{"injected registry create failure"};
        return inner.create();
    }

    void addFingerprint(
        audition::AcousticSourceId id,
        audition::SourceFingerprint value) override {
        if (fail_add) throw std::runtime_error{"injected fingerprint write failure"};
        inner.addFingerprint(id, std::move(value));
    }

    void associateSpeaker(
        audition::AcousticSourceId id,
        audition::SpeakerId speaker_id) override {
        inner.associateSpeaker(id, speaker_id);
    }

    std::optional<audition::AcousticSourceProfile> find(
        audition::AcousticSourceId id) const override {
        if (fail_find) throw std::runtime_error{"injected registry find failure"};
        return inner.find(id);
    }

    std::vector<audition::AcousticSourceProfile> list() const override {
        if (fail_list) throw std::runtime_error{"injected registry list failure"};
        return inner.list();
    }

    audition::InMemoryAcousticSourceRegistry inner{};
    bool fail_create{false};
    bool fail_add{false};
    bool fail_find{false};
    bool fail_list{false};
};

}  // namespace

TEST(SourceIdentityResolver, FailedActiveFingerprintWritePreservesGeometryAndClock) {
    for (const bool fail_on_find : {false, true}) {
        FaultingRegistry registry;
        audition::HeuristicSourceIdentityResolver resolver{registry};
        auto original = observation(1U, 10);
        original.position = position(2.0, 0.0, 0.0);
        const auto known = resolver.observe(original);

        auto failed = observation(1U, 1'000);
        failed.position = position(40.0, 0.0, 0.0);
        failed.fingerprint = fingerprint("model-a", {1.0F, 0.0F});
        registry.fail_find = fail_on_find;
        registry.fail_add = !fail_on_find;
        EXPECT_THROW(static_cast<void>(resolver.observe(failed)),
                     std::runtime_error);
        registry.fail_find = false;
        registry.fail_add = false;

        // Failure must not advance the global clock nor replace the
        // previous geometry used for post-retirement reacquisition.
        EXPECT_NO_THROW(resolver.endTrack(original.track_id, ts(11)));
        auto recovered = observation(2U, 12);
        recovered.position = original.position;
        const auto decision = resolver.observe(recovered);
        EXPECT_FALSE(decision.newly_created);
        EXPECT_EQ(decision.source_id, known.source_id);
        EXPECT_EQ(registry.inner.list().size(), 1U);
    }
}

TEST(SourceIdentityResolver, FailedNewFingerprintWriteDoesNotBindTrack) {
    FaultingRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto failed = observation(91U, 100);
    failed.fingerprint = fingerprint("model-b", {1.0F, 0.0F});
    registry.fail_add = true;
    EXPECT_THROW(static_cast<void>(resolver.observe(failed)),
                 std::runtime_error);
    registry.fail_add = false;

    // The first create() cannot be undone by the registry interface,
    // but the rejected track must NOT survive in resolver active state.
    auto retry = observation(91U, 99);
    const auto accepted = resolver.observe(retry);
    EXPECT_TRUE(accepted.newly_created);
    EXPECT_TRUE(accepted.source_id.valid());
    EXPECT_EQ(registry.inner.list().size(), 2U);
    const auto same = resolver.observe(observation(91U, 101));
    EXPECT_EQ(same.source_id, accepted.source_id);
    EXPECT_FALSE(same.newly_created);
}

TEST(SourceIdentityResolver, FailedRegistryListDoesNotEvictRecentGeometry) {
    FaultingRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions opts{};
    opts.max_reacquisition_age = audition::Duration{100};
    audition::HeuristicSourceIdentityResolver resolver{registry, opts};

    auto first = observation(1U, 0);
    first.position = position(3.0, 0.0, 0.0);
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1));

    registry.fail_list = true;
    EXPECT_THROW(
        static_cast<void>(resolver.observe(observation(2U, 10'000))),
        std::runtime_error);
    registry.fail_list = false;
    auto near = observation(2U, 2);
    near.position = first.position;
    const auto reacquired = resolver.observe(near);
    EXPECT_FALSE(reacquired.newly_created);
    EXPECT_EQ(reacquired.source_id, original.source_id);
    EXPECT_EQ(registry.inner.list().size(), 1U);
}

TEST(SourceIdentityResolver, FailedCreateDoesNotAdvanceClockOrBindTrack) {
    FaultingRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    registry.fail_create = true;
    EXPECT_THROW(
        static_cast<void>(resolver.observe(observation(4U, 500))),
        std::runtime_error);
    registry.fail_create = false;
    const auto accepted = resolver.observe(observation(4U, 1));
    EXPECT_TRUE(accepted.newly_created);
    EXPECT_EQ(registry.inner.list().size(), 1U);
}

TEST(SourceIdentityResolver, CreatesSourceAndKeepsActiveTrackContinuity) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(10U, 0);
    const auto first_decision = resolver.observe(first);
    EXPECT_TRUE(first_decision.newly_created);
    EXPECT_TRUE(first_decision.source_id.valid());
    EXPECT_DOUBLE_EQ(first_decision.confidence.value(), 0.0);

    auto second = observation(10U, 100'000'000);
    second.direction.direction =
        audition::Direction3D::fromVector({0.0, 1.0, 0.0});

    const auto second_decision = resolver.observe(second);
    EXPECT_FALSE(second_decision.newly_created);
    EXPECT_EQ(second_decision.source_id, first_decision.source_id);
    EXPECT_DOUBLE_EQ(second_decision.confidence.value(), 1.0);
    EXPECT_EQ(registry.list().size(), 1U);
}

TEST(SourceIdentityResolver, ReacquiresEndedTrackByFingerprint) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("clap-source-v1", {1.0F, 0.0F});
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1'000'000'000));

    auto replacement = observation(2U, 1'100'000'000);
    replacement.fingerprint =
        fingerprint("clap-source-v1", {0.999F, 0.02F});

    const auto decision = resolver.observe(replacement);
    EXPECT_FALSE(decision.newly_created);
    EXPECT_EQ(decision.source_id, original.source_id);
    EXPECT_GT(decision.confidence.value(), 0.95);
}

TEST(SourceIdentityResolver, ResetPreservesRegistryFingerprintMemory) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 5'000'000'000);
    first.fingerprint = fingerprint("source-embed", {0.6F, 0.8F});
    const auto original = resolver.observe(first);

    resolver.reset();

    auto replacement = observation(99U, 0);
    replacement.fingerprint = fingerprint("source-embed", {0.61F, 0.79F});
    const auto decision = resolver.observe(replacement);

    EXPECT_EQ(decision.source_id, original.source_id);
    EXPECT_FALSE(decision.newly_created);
    EXPECT_EQ(registry.list().size(), 1U);
}

TEST(SourceIdentityResolver, ReacquiresRecentSourceByGeometry) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.position = position(2.0, 1.0, 0.5);
    first.range = audition::RangeEstimate{
        {2.30, 0.01},
        audition::Probability::one(),
        audition::RangeEstimate::Method::Fused};
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(500'000'000));

    auto replacement = observation(2U, 600'000'000);
    replacement.position = position(2.05, 1.02, 0.5);
    replacement.range = audition::RangeEstimate{
        {2.34, 0.01},
        audition::Probability::one(),
        audition::RangeEstimate::Method::Fused};

    const auto decision = resolver.observe(replacement);
    EXPECT_EQ(decision.source_id, original.source_id);
    EXPECT_FALSE(decision.newly_created);
}

TEST(SourceIdentityResolver, DoesNotReacquireStaleGeometry) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.position = position(1.0, 0.0, 0.0);
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1'000'000'000));

    auto replacement = observation(2U, 7'000'000'000);
    replacement.position = position(1.0, 0.0, 0.0);
    const auto decision = resolver.observe(replacement);

    EXPECT_NE(decision.source_id, original.source_id);
    EXPECT_TRUE(decision.newly_created);
}

TEST(SourceIdentityResolver, PreventsTwoActiveTracksSharingOneSource) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto first_decision = resolver.observe(first);

    auto simultaneous = observation(2U, 10'000'000);
    simultaneous.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto second_decision = resolver.observe(simultaneous);

    EXPECT_NE(second_decision.source_id, first_decision.source_id);
    EXPECT_TRUE(second_decision.newly_created);
    EXPECT_EQ(registry.list().size(), 2U);
}

TEST(SourceIdentityResolver, FingerprintConflictPreventsGeometrySwap) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto left = observation(1U, 0);
    left.position = position(-1.0, 0.0, 0.0);
    left.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto left_source = resolver.observe(left);

    auto right = observation(2U, 10'000'000);
    right.position = position(1.0, 0.0, 0.0);
    right.fingerprint = fingerprint("source-embed", {0.0F, 1.0F});
    const auto right_source = resolver.observe(right);

    resolver.endTrack(left.track_id, ts(20'000'000));
    resolver.endTrack(right.track_id, ts(30'000'000));

    auto crossed = observation(3U, 40'000'000);
    crossed.position = position(1.0, 0.0, 0.0);
    crossed.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});

    const auto decision = resolver.observe(crossed);
    EXPECT_EQ(decision.source_id, left_source.source_id);
    EXPECT_NE(decision.source_id, right_source.source_id);
}

TEST(SourceIdentityResolver, IncompatibleFingerprintSpaceDoesNotReidentify) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("model-a", {1.0F, 0.0F});
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(10'000'000));

    auto replacement = observation(2U, 20'000'000);
    replacement.fingerprint = fingerprint("model-b", {1.0F, 0.0F});
    const auto decision = resolver.observe(replacement);

    EXPECT_NE(decision.source_id, original.source_id);
    EXPECT_TRUE(decision.newly_created);
}

TEST(SourceIdentityResolver, LowQualityFingerprintIsNotPersisted) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("source-embed", {1.0F, 0.0F}, 0.10);
    const auto original = resolver.observe(first);

    const auto stored = registry.find(original.source_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->fingerprints.empty());

    resolver.reset();

    auto replacement = observation(2U, 0);
    replacement.fingerprint =
        fingerprint("source-embed", {1.0F, 0.0F}, 0.10);
    const auto decision = resolver.observe(replacement);
    EXPECT_NE(decision.source_id, original.source_id);
}

TEST(SourceIdentityResolver, StoresAtMostOneFingerprintPerModel) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto source = resolver.observe(first);

    auto second = observation(1U, 10'000'000);
    second.fingerprint = fingerprint("source-embed", {0.9F, 0.1F});
    static_cast<void>(resolver.observe(second));

    const auto stored = registry.find(source.source_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->fingerprints.size(), 1U);
}

TEST(SourceIdentityResolver, RejectsMalformedFingerprint) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto malformed = observation(1U, 0);
    malformed.fingerprint = fingerprint("", {1.0F, 0.0F});

    EXPECT_THROW(
        static_cast<void>(resolver.observe(malformed)),
        audition::Error);
}

TEST(SourceIdentityResolver, RejectsClockChangeWithinStateEpoch) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    static_cast<void>(resolver.observe(observation(1U, 0)));

    auto different_clock = observation(1U, 1);
    different_clock.timestamp = ts(1, 7U);

    try {
        static_cast<void>(resolver.observe(different_clock));
        FAIL() << "Expected clock-domain mismatch";
    } catch (const audition::Error& error) {
        EXPECT_EQ(error.code(), audition::ErrorCode::ClockDomainMismatch);
    }
}

TEST(SourceIdentityResolver, RejectsOutOfOrderObservations) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    static_cast<void>(resolver.observe(observation(1U, 100)));

    try {
        static_cast<void>(resolver.observe(observation(1U, 99)));
        FAIL() << "Expected invalid time ordering";
    } catch (const audition::Error& error) {
        EXPECT_EQ(error.code(), audition::ErrorCode::InvalidArgument);
    }
}

TEST(SourceIdentityResolver, EndTrackIsIdempotentForUnknownTrack) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    resolver.endTrack(audition::SpatialTrackId{123U}, ts(0));
    resolver.endTrack(audition::SpatialTrackId{123U}, ts(1));
    EXPECT_TRUE(registry.list().empty());
}


TEST(SourceIdentityResolver, RejectsInvalidConfigurationBoundaries) {
    audition::InMemoryAcousticSourceRegistry registry;
    const auto invalid = [&](audition::HeuristicSourceIdentityResolverOptions opts) {
        EXPECT_THROW(
            static_cast<void>(audition::HeuristicSourceIdentityResolver{registry, opts}),
            audition::Error);
    };
    audition::HeuristicSourceIdentityResolverOptions opts{};
    opts.max_active_tracks = 0U;
    invalid(opts);
    opts = {};
    opts.max_recent_sources = 0U;
    invalid(opts);
    opts = {};
    opts.max_active_tracks = 3U;
    opts.max_recent_sources = 2U;
    invalid(opts);
    opts = {};
    opts.max_position_distance_m = 0.0;
    invalid(opts);
    opts = {};
    opts.max_range_delta_m = std::numeric_limits<double>::infinity();
    invalid(opts);
    opts = {};
    opts.max_direction_angle_rad = -1.0;
    invalid(opts);
    opts = {};
    opts.min_fingerprint_quality = -0.1;
    invalid(opts);
    opts = {};
    opts.association_threshold = 1.1;
    invalid(opts);
    opts = {};
    opts.min_fingerprint_cosine_similarity = -1.1;
    invalid(opts);
    opts = {};
    opts.fingerprint_weight = -0.1;
    invalid(opts);
    opts = {};
    opts.fingerprint_weight = 0.0;
    opts.position_weight = 0.0;
    opts.range_weight = 0.0;
    opts.direction_weight = 0.0;
    invalid(opts);
}

TEST(SourceIdentityResolver, CapacityFailureDoesNotReplaceActiveIdentity) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions opts{};
    opts.max_active_tracks = 1U;
    opts.max_recent_sources = 2U;
    audition::HeuristicSourceIdentityResolver resolver{registry, opts};
    const auto original = resolver.observe(observation(1U, 0));
    EXPECT_THROW(static_cast<void>(resolver.observe(observation(2U, 1))),
                 audition::Error);
    EXPECT_EQ(registry.list().size(), 1U);
    const auto updated = resolver.observe(observation(1U, 2));
    EXPECT_EQ(updated.source_id, original.source_id);
    EXPECT_FALSE(updated.newly_created);
    resolver.endTrack(audition::SpatialTrackId{1U}, ts(3));
    const auto replacement = resolver.observe(observation(2U, 4));
    EXPECT_TRUE(replacement.newly_created);
    EXPECT_NE(replacement.source_id, original.source_id);
}


TEST(SourceIdentityResolver, CapacityRejectionDoesNotAdvanceClock) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.max_active_tracks = 1U;
    options.max_recent_sources = 2U;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    const auto original = resolver.observe(observation(1U, 10));
    // A rejected newer input must not make the still-active track's next
    // perfectly valid input look globally out of order.
    EXPECT_THROW(
        static_cast<void>(resolver.observe(observation(2U, 10'000))),
        audition::Error);
    const auto next = resolver.observe(observation(1U, 11));
    EXPECT_EQ(next.source_id, original.source_id);
    EXPECT_FALSE(next.newly_created);
    resolver.endTrack(audition::SpatialTrackId{1U}, ts(12));
    EXPECT_TRUE(resolver.observe(observation(2U, 13)).newly_created);
}

TEST(SourceIdentityResolver, RejectedAdmissionDoesNotEvictRecentSource) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.max_active_tracks = 1U;
    options.max_recent_sources = 3U;
    options.max_reacquisition_age = audition::Duration{100};
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto earlier = observation(1U, 0);
    earlier.position = position(1.0, 0.0, 0.0);
    const auto known = resolver.observe(earlier);
    resolver.endTrack(earlier.track_id, ts(1));

    auto occupied = observation(2U, 2);
    occupied.position = position(100.0, 0.0, 0.0);
    static_cast<void>(resolver.observe(occupied));
    EXPECT_THROW(
        static_cast<void>(resolver.observe(observation(3U, 10'000))),
        audition::Error);
    // Failed admission must neither expire the first track's position
    // evidence nor advance the global clock.
    EXPECT_NO_THROW(
        static_cast<void>(resolver.observe(observation(2U, 3))));
    resolver.endTrack(occupied.track_id, ts(4));

    auto recovered = observation(3U, 5);
    recovered.position = earlier.position;
    const auto match = resolver.observe(recovered);
    EXPECT_FALSE(match.newly_created);
    EXPECT_EQ(match.source_id, known.source_id);
}

TEST(SourceIdentityResolver, ExtremeTimestampSpanCannotOverflowSourceAge) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.max_reacquisition_age = audition::Duration{100};
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    constexpr auto first_time = std::numeric_limits<std::int64_t>::min() + 10;
    constexpr auto last_time = std::numeric_limits<std::int64_t>::max() - 10;
    auto original = observation(1U, first_time);
    original.position = position(2.0, 0.0, 0.0);
    const auto old_source = resolver.observe(original);
    resolver.endTrack(original.track_id, ts(first_time + 1));

    auto new_track = observation(2U, last_time);
    new_track.position = original.position;
    const auto newer = resolver.observe(new_track);
    EXPECT_TRUE(newer.newly_created);
    EXPECT_NE(newer.source_id, old_source.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}

TEST(SourceIdentityResolver, RejectsZeroTrackAndInvalidFingerprintVectors) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto bad = observation(0U, 0);
    EXPECT_THROW(static_cast<void>(resolver.observe(bad)), audition::Error);
    bad = observation(1U, 0);
    bad.fingerprint = fingerprint("embedding", {});
    EXPECT_THROW(static_cast<void>(resolver.observe(bad)), audition::Error);
    bad.fingerprint = fingerprint("embedding", {0.0F, 0.0F});
    EXPECT_THROW(static_cast<void>(resolver.observe(bad)), audition::Error);
    bad.fingerprint = fingerprint(
        "embedding", {std::numeric_limits<float>::quiet_NaN(), 1.0F});
    EXPECT_THROW(static_cast<void>(resolver.observe(bad)), audition::Error);
    bad.fingerprint = fingerprint(
        "embedding", {std::numeric_limits<float>::infinity(), 1.0F});
    EXPECT_THROW(static_cast<void>(resolver.observe(bad)), audition::Error);
    EXPECT_TRUE(registry.list().empty());
}

TEST(SourceIdentityResolver, RecycledTrackIdAfterEndIsNewGenerationWithoutEvidence) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto first = observation(7U, 0);
    first.fingerprint = fingerprint("model-a", {1.0F, 0.0F});
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1));
    auto recycled = observation(7U, 2);
    recycled.fingerprint = fingerprint("model-b", {0.0F, 1.0F});
    const auto next = resolver.observe(recycled);
    EXPECT_TRUE(next.newly_created);
    EXPECT_NE(next.source_id, original.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, FailedEndTrackPreservesActiveTrackBinding) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    const auto first = resolver.observe(observation(41U, 100));

    EXPECT_THROW(
        resolver.endTrack(audition::SpatialTrackId{41U}, ts(99)),
        audition::Error);
    EXPECT_THROW(
        resolver.endTrack(audition::SpatialTrackId{41U}, ts(101, 7U)),
        audition::Error);

    const auto continued = resolver.observe(observation(41U, 102));
    EXPECT_EQ(continued.source_id, first.source_id);
    EXPECT_FALSE(continued.newly_created);
    EXPECT_DOUBLE_EQ(continued.confidence.value(), 1.0);
    EXPECT_EQ(registry.list().size(), 1U);

    resolver.endTrack(audition::SpatialTrackId{41U}, ts(103));
    const auto recycled = resolver.observe(observation(41U, 104));
    EXPECT_TRUE(recycled.newly_created);
    EXPECT_NE(recycled.source_id, first.source_id);
}


TEST(SourceIdentityResolver, ReacquiredSourceCannotBeSharedByAnotherActiveTrack) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto initial = observation(1U, 0);
    initial.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto source = resolver.observe(initial);
    resolver.endTrack(initial.track_id, ts(10));

    auto reacquired = observation(2U, 20);
    reacquired.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto returned = resolver.observe(reacquired);
    EXPECT_EQ(returned.source_id, source.source_id);
    EXPECT_FALSE(returned.newly_created);

    auto simultaneous = observation(3U, 30);
    simultaneous.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto competing = resolver.observe(simultaneous);
    EXPECT_TRUE(competing.newly_created);
    EXPECT_NE(competing.source_id, source.source_id);

    const auto still_active = resolver.observe(observation(2U, 40));
    EXPECT_EQ(still_active.source_id, source.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, PositionOnlyReacquisitionRejectsDistantCandidate) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto original = observation(1U, 0);
    original.position = position(1.0, 2.0, 3.0);
    const auto first = resolver.observe(original);
    resolver.endTrack(original.track_id, ts(10));

    auto distant = observation(2U, 20);
    distant.position = position(100.0, 2.0, 3.0);
    const auto unrelated = resolver.observe(distant);
    EXPECT_TRUE(unrelated.newly_created);
    EXPECT_NE(unrelated.source_id, first.source_id);
    resolver.endTrack(distant.track_id, ts(30));

    auto nearby = observation(3U, 40);
    nearby.position = position(1.0, 2.0, 3.0);
    const auto recovered = resolver.observe(nearby);
    EXPECT_FALSE(recovered.newly_created);
    EXPECT_EQ(recovered.source_id, first.source_id);
    EXPECT_NE(recovered.source_id, unrelated.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, RangeWithoutFingerprintOrPositionCannotReacquire) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto first = observation(1U, 0);
    first.range = audition::RangeEstimate{
        {2.0, 0.1}, audition::Probability::one(),
        audition::RangeEstimate::Method::Fused};
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(10));

    auto second = observation(2U, 20);
    second.range = first.range;
    const auto decision = resolver.observe(second);
    EXPECT_TRUE(decision.newly_created);
    EXPECT_NE(decision.source_id, original.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, DirectionOnlyCannotReacquireEndedSource) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto first = observation(1U, 0);
    first.direction.direction =
        audition::Direction3D::fromVector({0.0, 1.0, 0.0});
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(10));

    auto second = observation(2U, 20);
    second.direction.direction = first.direction.direction;
    const auto decision = resolver.observe(second);
    EXPECT_TRUE(decision.newly_created);
    EXPECT_NE(decision.source_id, original.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, LowSimilarityFingerprintDoesNotOverrideMatchingPosition) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.position = position(1.0, 2.0, 3.0);
    first.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(10));

    auto incompatible = observation(2U, 20);
    incompatible.position = first.position;
    incompatible.fingerprint = fingerprint("source-embed", {0.0F, 1.0F});
    const auto decision = resolver.observe(incompatible);
    EXPECT_TRUE(decision.newly_created);
    EXPECT_NE(decision.source_id, original.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, WeakFingerprintQualityDoesNotVetoStrongPositionMatch) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};

    auto first = observation(1U, 0);
    first.position = position(1.0, 2.0, 3.0);
    first.fingerprint = fingerprint("source-embed", {1.0F, 0.0F});
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(10));

    auto replacement = observation(2U, 20);
    replacement.position = first.position;
    replacement.fingerprint = fingerprint(
        "source-embed", {0.0F, 1.0F}, 0.10);
    const auto decision = resolver.observe(replacement);
    EXPECT_FALSE(decision.newly_created);
    EXPECT_EQ(decision.source_id, original.source_id);
    EXPECT_EQ(registry.list().size(), 1U);
    const auto stored = registry.find(original.source_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->fingerprints.size(), 1U);
}


TEST(SourceIdentityResolver, PositionReacquisitionExpiresStrictlyAfterAgeLimit) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.max_reacquisition_age = audition::Duration{100};
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto initial = observation(1U, 0);
    initial.position = position(1.0, 2.0, 3.0);
    const auto original = resolver.observe(initial);
    resolver.endTrack(initial.track_id, ts(10));

    auto within_window = observation(2U, 109);
    within_window.position = initial.position;
    const auto reacquired = resolver.observe(within_window);
    // Geometry confidence is recency-weighted; near expiry it no longer
    // reaches the association threshold even before pruning.
    EXPECT_TRUE(reacquired.newly_created);
    EXPECT_NE(reacquired.source_id, original.source_id);
    resolver.endTrack(within_window.track_id, ts(110));

    auto expired = observation(3U, 211);
    expired.position = initial.position;
    const auto fresh = resolver.observe(expired);
    EXPECT_TRUE(fresh.newly_created);
    EXPECT_NE(fresh.source_id, original.source_id);
    EXPECT_NE(fresh.source_id, reacquired.source_id);
}


TEST(SourceIdentityResolver, EqualFingerprintCandidatesPreferLowestFreeSourceId) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    // Ignore direction/geometry recency so identical fingerprints produce
    // genuinely equal candidate scores, regardless of endTrack timestamps.
    options.fingerprint_weight = 1.0;
    options.position_weight = 0.0;
    options.range_weight = 0.0;
    options.direction_weight = 0.0;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("same-model", {1.0F, 0.0F});
    const auto original_a = resolver.observe(first);

    // While source A is active, identical evidence must not claim its ID.
    auto second = observation(2U, 1);
    second.fingerprint = first.fingerprint;
    const auto original_b = resolver.observe(second);
    ASSERT_NE(original_a.source_id, original_b.source_id);
    resolver.endTrack(first.track_id, ts(2));
    resolver.endTrack(second.track_id, ts(3));

    auto replacement_a = observation(3U, 4);
    replacement_a.fingerprint = first.fingerprint;
    const auto matched_a = resolver.observe(replacement_a);
    EXPECT_FALSE(matched_a.newly_created);
    EXPECT_EQ(matched_a.source_id, original_a.source_id);

    // Once A is bound again, the same observation must fall back to B.
    auto replacement_b = observation(4U, 5);
    replacement_b.fingerprint = first.fingerprint;
    const auto matched_b = resolver.observe(replacement_b);
    EXPECT_FALSE(matched_b.newly_created);
    EXPECT_EQ(matched_b.source_id, original_b.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, StrongerFingerprintOutranksLowerSourceId) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.fingerprint_weight = 1.0;
    options.position_weight = 0.0;
    options.range_weight = 0.0;
    options.direction_weight = 0.0;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto first = observation(11U, 0);
    first.fingerprint = fingerprint("same-model", {0.92F, 0.392F});
    const auto weaker = resolver.observe(first);

    auto second = observation(12U, 1);
    second.fingerprint = fingerprint("same-model", {1.0F, 0.0F});
    const auto stronger = resolver.observe(second);
    ASSERT_NE(weaker.source_id, stronger.source_id);
    resolver.endTrack(first.track_id, ts(2));
    resolver.endTrack(second.track_id, ts(3));

    auto query = observation(13U, 4);
    query.fingerprint = fingerprint("same-model", {1.0F, 0.0F});
    const auto selected = resolver.observe(query);
    EXPECT_FALSE(selected.newly_created);
    EXPECT_EQ(selected.source_id, stronger.source_id);

    // The better-scoring candidate is now occupied; the next observation
    // can still recover the lower-ranked but compatible source.
    auto fallback = observation(14U, 5);
    fallback.fingerprint = query.fingerprint;
    const auto selected_fallback = resolver.observe(fallback);
    EXPECT_FALSE(selected_fallback.newly_created);
    EXPECT_EQ(selected_fallback.source_id, weaker.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}


TEST(SourceIdentityResolver, RecentGeometryCapacityEvictsLowestIdOnEqualAge) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.max_active_tracks = 1U;
    options.max_recent_sources = 2U;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto first = observation(1U, 0);
    first.position = position(0.0, 0.0, 0.0);
    const auto source_a = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(10));

    auto second = observation(2U, 10);
    second.position = position(10.0, 0.0, 0.0);
    const auto source_b = resolver.observe(second);
    ASSERT_NE(source_a.source_id, source_b.source_id);
    ASSERT_LT(source_a.source_id.value(), source_b.source_id.value());
    resolver.endTrack(second.track_id, ts(10));

    // A and B have identical last-seen times. Inserting C must evict A's
    // transient geometry by the source-ID tie-break, not the hash-map order.
    auto third = observation(3U, 11);
    third.position = position(20.0, 0.0, 0.0);
    const auto source_c = resolver.observe(third);
    EXPECT_TRUE(source_c.newly_created);
    resolver.endTrack(third.track_id, ts(11));

    auto recover_b = observation(4U, 12);
    recover_b.position = second.position;
    const auto reacquired_b = resolver.observe(recover_b);
    EXPECT_FALSE(reacquired_b.newly_created);
    EXPECT_EQ(reacquired_b.source_id, source_b.source_id);
    resolver.endTrack(recover_b.track_id, ts(13));

    auto recover_a = observation(5U, 14);
    recover_a.position = first.position;
    const auto replacement_a = resolver.observe(recover_a);
    EXPECT_TRUE(replacement_a.newly_created);
    EXPECT_NE(replacement_a.source_id, source_a.source_id);
    EXPECT_EQ(registry.list().size(), 4U);
    EXPECT_TRUE(registry.find(source_a.source_id).has_value());
}


TEST(SourceIdentityResolver, InvalidRangeMomentsDoNotCreateOrChangeIdentity) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto bad = observation(1U, 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto& [mean, variance] :
         {std::pair<double, double>{-1.0, 1.0}, {1.0, -1.0},
          {nan, 1.0}, {1.0, inf}}) {
        bad.range = audition::RangeEstimate{
            {mean, variance}, audition::Probability::one(),
            audition::RangeEstimate::Method::Fused};
        EXPECT_THROW(static_cast<void>(resolver.observe(bad)), audition::Error);
    }
    EXPECT_TRUE(registry.list().empty());
    bad.range.reset();
    EXPECT_TRUE(resolver.observe(bad).newly_created);
}

TEST(SourceIdentityResolver, NonfinitePositionCoordinatesRejectBeforeRegistryMutation) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto bad = observation(1U, 0);
    for (int axis = 0; axis != 3; ++axis) {
        bad.position = position(1.0, 2.0, 3.0);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (axis == 0) bad.position->mean_m.x = nan;
        if (axis == 1) bad.position->mean_m.y = nan;
        if (axis == 2) bad.position->mean_m.z = nan;
        EXPECT_THROW(static_cast<void>(resolver.observe(bad)), audition::Error);
    }
    EXPECT_TRUE(registry.list().empty());
    bad.position = position(1.0, 2.0, 3.0);
    EXPECT_TRUE(resolver.observe(bad).newly_created);
}

TEST(SourceIdentityResolver, InvalidIndividualEvidenceWeightsAreRejected) {
    audition::InMemoryAcousticSourceRegistry registry;
    for (int field = 0; field < 4; ++field) {
        audition::HeuristicSourceIdentityResolverOptions options{};
        double* weight = nullptr;
        if (field == 0) weight = &options.fingerprint_weight;
        if (field == 1) weight = &options.position_weight;
        if (field == 2) weight = &options.range_weight;
        if (field == 3) weight = &options.direction_weight;
        for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
            *weight = invalid;
            EXPECT_THROW(
                static_cast<void>(audition::HeuristicSourceIdentityResolver{registry, options}),
                audition::Error);
        }
    }
}

TEST(SourceIdentityResolver, FiniteIndividualWeightsWithOverflowingSumAreRejected) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.fingerprint_weight = std::numeric_limits<double>::max();
    options.position_weight = std::numeric_limits<double>::max();
    options.range_weight = 0.0;
    options.direction_weight = 0.0;
    EXPECT_THROW(
        static_cast<void>(audition::HeuristicSourceIdentityResolver{registry, options}),
        audition::Error);
}

TEST(SourceIdentityResolver, FingerprintAtExactQualityThresholdIsStored) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.min_fingerprint_quality = 0.25;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};
    auto initial = observation(1U, 0);
    initial.fingerprint = fingerprint("model", {1.F, 0.F}, 0.25);
    const auto first = resolver.observe(initial);
    auto stored = registry.find(first.source_id);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->fingerprints.size(), 1U);
    EXPECT_DOUBLE_EQ(stored->fingerprints.front().quality.value(), 0.25);
    resolver.endTrack(initial.track_id, ts(1));

    auto next = observation(2U, 2);
    next.fingerprint = fingerprint("model", {1.F, 0.F}, 0.25);
    const auto recovered = resolver.observe(next);
    EXPECT_FALSE(recovered.newly_created);
    EXPECT_EQ(recovered.source_id, first.source_id);
}

TEST(SourceIdentityResolver, ActiveTrackAccumulatesDistinctEmbeddingModels) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("model-a", {1.F, 0.F});
    const auto source = resolver.observe(first);
    auto second = observation(1U, 1);
    second.fingerprint = fingerprint("model-b", {0.F, 1.F});
    const auto updated = resolver.observe(second);
    EXPECT_EQ(updated.source_id, source.source_id);
    EXPECT_FALSE(updated.newly_created);
    const auto stored = registry.find(source.source_id);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->fingerprints.size(), 2U);
    resolver.endTrack(second.track_id, ts(2));

    auto next = observation(2U, 3);
    next.fingerprint = fingerprint("model-b", {0.F, 1.F});
    const auto recovered = resolver.observe(next);
    EXPECT_FALSE(recovered.newly_created);
    EXPECT_EQ(recovered.source_id, source.source_id);
}

TEST(SourceIdentityResolver, BestMatchingStoredFingerprintWinsWithinProfile) {
    audition::InMemoryAcousticSourceRegistry registry;
    const auto id = registry.create();
    registry.addFingerprint(id, fingerprint("model", {0.F, 1.F}));
    registry.addFingerprint(id, fingerprint("model", {1.F, 0.F}));
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto query = observation(1U, 0);
    query.fingerprint = fingerprint("model", {1.F, 0.F});
    const auto matched = resolver.observe(query);
    EXPECT_FALSE(matched.newly_created);
    EXPECT_EQ(matched.source_id, id);
    EXPECT_EQ(registry.find(id)->fingerprints.size(), 2U);
}

TEST(SourceIdentityResolver, CorruptStoredFingerprintDoesNotPoisonHealthyExample) {
    audition::InMemoryAcousticSourceRegistry registry;
    const auto id = registry.create();
    registry.addFingerprint(id, fingerprint("model", {0.F, 0.F}));
    registry.addFingerprint(id, fingerprint("model", {1.F, 0.F}));
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto query = observation(1U, 0);
    query.fingerprint = fingerprint("model", {1.F, 0.F});
    const auto matched = resolver.observe(query);
    EXPECT_FALSE(matched.newly_created);
    EXPECT_EQ(matched.source_id, id);
}

TEST(SourceIdentityResolver, LowQualityStoredFingerprintCannotAnchorAssociation) {
    audition::InMemoryAcousticSourceRegistry registry;
    const auto id = registry.create();
    registry.addFingerprint(id, fingerprint("model", {1.F, 0.F}, 0.1));
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto query = observation(1U, 0);
    query.fingerprint = fingerprint("model", {1.F, 0.F});
    const auto result = resolver.observe(query);
    EXPECT_TRUE(result.newly_created);
    EXPECT_NE(result.source_id, id);
}

TEST(SourceIdentityResolver, FingerprintMemoryOutlivesTransientGeometryCache) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.max_reacquisition_age = audition::Duration{100};
    audition::HeuristicSourceIdentityResolver resolver{registry, options};
    auto first = observation(1U, 0);
    first.fingerprint = fingerprint("model", {1.F, 0.F});
    first.position = position(1.0, 2.0, 3.0);
    const auto source = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1));

    auto late = observation(2U, 1000);
    late.fingerprint = first.fingerprint;
    late.position = position(50.0, 60.0, 70.0);
    const auto matched = resolver.observe(late);
    EXPECT_FALSE(matched.newly_created);
    EXPECT_EQ(matched.source_id, source.source_id);
}

TEST(SourceIdentityResolver, ExactMaximumPositionDistanceIsBoundaryEvidence) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.fingerprint_weight = 0.0;
    options.position_weight = 1.0;
    options.range_weight = 0.0;
    options.direction_weight = 0.0;
    options.association_threshold = 0.0;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};
    auto first = observation(1U, 0);
    first.position = position(0.0, 0.0, 0.0);
    const auto created = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1));

    auto edge = observation(2U, 2);
    edge.position = position(options.max_position_distance_m, 0.0, 0.0);
    const auto matched = resolver.observe(edge);
    EXPECT_FALSE(matched.newly_created);
    EXPECT_EQ(matched.source_id, created.source_id);
    EXPECT_DOUBLE_EQ(matched.confidence.value(), 0.0);
}

TEST(SourceIdentityResolver, PositionSimilarityMustClearAssociationThreshold) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.fingerprint_weight = 0.0;
    options.position_weight = 1.0;
    options.range_weight = 0.0;
    options.direction_weight = 0.0;
    options.association_threshold = 0.9;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};
    auto first = observation(1U, 0);
    first.position = position(0.0, 0.0, 0.0);
    const auto original = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1));

    auto noisy = observation(2U, 2);
    noisy.position = position(0.3, 0.0, 0.0);
    const auto created = resolver.observe(noisy);
    EXPECT_TRUE(created.newly_created);
    EXPECT_NE(created.source_id, original.source_id);
}

TEST(SourceIdentityResolver, RangeRefinesPositionAnchoredCandidateRanking) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.fingerprint_weight = 0.0;
    options.position_weight = 0.5;
    options.range_weight = 0.5;
    options.direction_weight = 0.0;
    options.association_threshold = 0.5;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto weak = observation(1U, 0);
    weak.position = position(1.0, 2.0, 3.0);
    weak.range = audition::RangeEstimate{
        {2.9, 0.1}, audition::Probability::one(),
        audition::RangeEstimate::Method::Fused};
    const auto weak_source = resolver.observe(weak);
    auto strong = observation(2U, 1);
    strong.position = weak.position;
    strong.range = audition::RangeEstimate{
        {2.0, 0.1}, audition::Probability::one(),
        audition::RangeEstimate::Method::Fused};
    const auto strong_source = resolver.observe(strong);
    ASSERT_NE(weak_source.source_id, strong_source.source_id);
    resolver.endTrack(weak.track_id, ts(2));
    resolver.endTrack(strong.track_id, ts(2));

    auto query = observation(3U, 3);
    query.position = strong.position;
    query.range = strong.range;
    const auto selected = resolver.observe(query);
    EXPECT_FALSE(selected.newly_created);
    EXPECT_EQ(selected.source_id, strong_source.source_id);
}

TEST(SourceIdentityResolver, PositionReacquisitionSurvivesDirectionDisabled) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.fingerprint_weight = 0.0;
    options.position_weight = 1.0;
    options.range_weight = 0.0;
    options.direction_weight = 0.0;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto first = observation(1U, 10);
    first.position = position(1.0, 2.0, 3.0);
    const auto initial = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(11));

    auto later = observation(2U, 12);
    later.position = first.position;
    const auto recovered = resolver.observe(later);
    EXPECT_FALSE(recovered.newly_created);
    EXPECT_EQ(recovered.source_id, initial.source_id);
}

TEST(SourceIdentityResolver, DirectionRefinesPositionAnchoredCandidateRanking) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.fingerprint_weight = 0.0;
    options.position_weight = 0.5;
    options.range_weight = 0.0;
    options.direction_weight = 0.5;
    options.association_threshold = 0.5;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};

    auto weaker = observation(1U, 0);
    weaker.position = position(1.0, 2.0, 3.0);
    weaker.direction.direction =
        audition::Direction3D::fromVector({0.95, 0.3122498999199199, 0.0});
    const auto old = resolver.observe(weaker);
    auto stronger = observation(2U, 1);
    stronger.position = weaker.position;
    stronger.direction.direction =
        audition::Direction3D::fromVector({1.0, 0.0, 0.0});
    const auto better = resolver.observe(stronger);
    resolver.endTrack(weaker.track_id, ts(2));
    resolver.endTrack(stronger.track_id, ts(2));

    auto query = observation(3U, 3);
    query.position = stronger.position;
    query.direction.direction = stronger.direction.direction;
    const auto selected = resolver.observe(query);
    EXPECT_FALSE(selected.newly_created);
    EXPECT_EQ(selected.source_id, better.source_id);
    EXPECT_NE(selected.source_id, old.source_id);
}

TEST(SourceIdentityResolver, ResetPermitsNewClockAndTrackGeneration) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    auto first = observation(1U, 100);
    const auto previous = resolver.observe(first);
    resolver.reset();

    auto reused = observation(1U, 0);
    reused.timestamp = ts(0, 5U);
    const auto next = resolver.observe(reused);
    EXPECT_TRUE(next.newly_created);
    EXPECT_NE(next.source_id, previous.source_id);
    EXPECT_EQ(registry.list().size(), 2U);
}

TEST(SourceIdentityResolver, RecentCacheEvictsOldestRatherThanSmallestId) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolverOptions options{};
    options.max_active_tracks = 1U;
    options.max_recent_sources = 2U;
    audition::HeuristicSourceIdentityResolver resolver{registry, options};
    auto first = observation(1U, 0);
    first.position = position(1.0, 0.0, 0.0);
    const auto source_a = resolver.observe(first);
    resolver.endTrack(first.track_id, ts(1));

    auto second = observation(2U, 2);
    second.position = position(10.0, 0.0, 0.0);
    const auto source_b = resolver.observe(second);
    resolver.endTrack(second.track_id, ts(3));

    auto refresh_a = observation(3U, 4);
    refresh_a.position = first.position;
    const auto refreshed = resolver.observe(refresh_a);
    ASSERT_FALSE(refreshed.newly_created);
    ASSERT_EQ(refreshed.source_id, source_a.source_id);
    resolver.endTrack(refresh_a.track_id, ts(5));

    auto third = observation(4U, 6);
    third.position = position(20.0, 0.0, 0.0);
    const auto source_c = resolver.observe(third);
    EXPECT_TRUE(source_c.newly_created);
    EXPECT_NE(source_c.source_id, source_b.source_id);
    resolver.endTrack(third.track_id, ts(6));

    auto recover_a = observation(5U, 7);
    recover_a.position = first.position;
    const auto stable = resolver.observe(recover_a);
    EXPECT_FALSE(stable.newly_created);
    EXPECT_EQ(stable.source_id, source_a.source_id);
    resolver.endTrack(recover_a.track_id, ts(8));

    auto recover_b = observation(6U, 9);
    recover_b.position = second.position;
    const auto missed = resolver.observe(recover_b);
    EXPECT_TRUE(missed.newly_created);
    EXPECT_NE(missed.source_id, source_b.source_id);
}
