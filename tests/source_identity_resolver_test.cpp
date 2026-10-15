#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/source_identity_resolver.hpp>

#include <cstdint>
#include <initializer_list>
#include <limits>
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

}  // namespace

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
