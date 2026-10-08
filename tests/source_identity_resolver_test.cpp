#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/source_identity_resolver.hpp>

#include <cstdint>
#include <initializer_list>
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
