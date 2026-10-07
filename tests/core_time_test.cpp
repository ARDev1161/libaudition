#include <audition/core/time.hpp>

#include <gtest/gtest.h>

TEST(Time, DifferenceRequiresSameClock) {
    const audition::Timestamp a{100, {audition::ClockDomain::Simulation, 1}};
    const audition::Timestamp b{150, {audition::ClockDomain::Simulation, 1}};
    EXPECT_EQ(b.since(a).nanoseconds(), 50);

    const audition::Timestamp external{150, {audition::ClockDomain::External, 7}};
    EXPECT_THROW(external.since(a), audition::Error);
}

TEST(Time, AdvancePreservesClock) {
    const audition::Timestamp t{100, {audition::ClockDomain::External, 9}};
    const auto advanced = t.advancedBy(audition::Duration{25});
    EXPECT_EQ(advanced.nanoseconds(), 125);
    EXPECT_EQ(advanced.clock().source_id, 9U);
}
