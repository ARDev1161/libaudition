#include <audition/core/time.hpp>

#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

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

TEST(Time, DurationArithmeticRejectsSignedOverflow) {
    using audition::Duration;
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    EXPECT_EQ((Duration{high - 1} + Duration{1}).nanoseconds(), high);
    EXPECT_EQ((Duration{low + 1} - Duration{1}).nanoseconds(), low);
    EXPECT_EQ((Duration{low + 1} + Duration{-1}).nanoseconds(), low);
    EXPECT_EQ((Duration{high - 1} - Duration{-1}).nanoseconds(), high);
    EXPECT_THROW((void)(Duration{high} + Duration{1}), std::overflow_error);
    EXPECT_THROW((void)(Duration{low} + Duration{-1}), std::overflow_error);
    EXPECT_THROW((void)(Duration{high} - Duration{-1}), std::overflow_error);
    EXPECT_THROW((void)(Duration{low} - Duration{1}), std::overflow_error);
}

TEST(Time, TimestampArithmeticRejectsSignedOverflow) {
    using audition::Duration;
    using audition::Timestamp;
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    const audition::ClockIdentity clock{audition::ClockDomain::External, 42U};
    const Timestamp top{high, clock};
    const Timestamp bottom{low, clock};
    EXPECT_EQ(top.advancedBy(Duration{-1}).nanoseconds(), high - 1);
    EXPECT_EQ(bottom.advancedBy(Duration{1}).nanoseconds(), low + 1);
    EXPECT_THROW((void)top.advancedBy(Duration{1}), std::overflow_error);
    EXPECT_THROW((void)bottom.advancedBy(Duration{-1}), std::overflow_error);
    EXPECT_THROW((void)top.since(bottom), std::overflow_error);
    EXPECT_THROW((void)bottom.since(top), std::overflow_error);
    EXPECT_EQ(top.clock(), clock);
    EXPECT_EQ(bottom.clock(), clock);
}
