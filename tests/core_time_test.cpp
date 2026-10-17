#include <audition/core/time.hpp>

#include <gtest/gtest.h>

TEST(Time, DifferenceRequiresSameClock) {
    const audition::Timestamp a{100, {audition::ClockDomain::Simulation, 1}};
    const audition::Timestamp b{150, {audition::ClockDomain::Simulation, 1}};
    EXPECT_EQ(b.since(a).nanoseconds(), 50);

    const audition::Timestamp external{150, {audition::ClockDomain::External, 7}};
    EXPECT_THROW(
        {
            const auto difference = external.since(a);
            (void)difference;
        },
        audition::Error);
}

TEST(Time, AdvancePreservesClock) {
    const audition::Timestamp t{100, {audition::ClockDomain::External, 9}};
    const auto advanced = t.advancedBy(audition::Duration{25});
    EXPECT_EQ(advanced.nanoseconds(), 125);
    EXPECT_EQ(advanced.clock().source_id, 9U);
}

TEST(Time, CurrentClockFactoriesAdvertiseCorrectDomains) {
    const auto system = audition::Timestamp::systemNow();
    const auto monotonic = audition::Timestamp::monotonicNow();
    EXPECT_EQ(system.clock().domain, audition::ClockDomain::SystemUtc);
    EXPECT_EQ(monotonic.clock().domain, audition::ClockDomain::Monotonic);
    EXPECT_EQ(system.clock().source_id, 0U);
    EXPECT_EQ(monotonic.clock().source_id, 0U);
    EXPECT_FALSE(system.comparableWith(monotonic));
}

TEST(Time, SignedDurationAndComparableClockIdentityArePreserved) {
    const audition::Timestamp before{-200,
        {audition::ClockDomain::Simulation, 42}};
    const audition::Timestamp after{100,
        {audition::ClockDomain::Simulation, 42}};
    EXPECT_TRUE(after.comparableWith(before));
    EXPECT_EQ(before.since(after).nanoseconds(), -300);
    EXPECT_EQ(after.since(before).nanoseconds(), 300);
    const auto moved = before.advancedBy(audition::Duration{-25});
    EXPECT_EQ(moved.nanoseconds(), -225);
    EXPECT_EQ(moved.clock(), before.clock());
    EXPECT_FALSE(after.comparableWith(
        audition::Timestamp{100, {audition::ClockDomain::Simulation, 43}}));
}
