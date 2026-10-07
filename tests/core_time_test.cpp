#include <acoustic/core/time.hpp>

#include <gtest/gtest.h>

TEST(Time, DifferenceRequiresSameClock) {
    const acoustic::Timestamp a{100, {acoustic::ClockDomain::Simulation, 1}};
    const acoustic::Timestamp b{150, {acoustic::ClockDomain::Simulation, 1}};
    EXPECT_EQ(b.since(a).nanoseconds(), 50);

    const acoustic::Timestamp external{150, {acoustic::ClockDomain::External, 7}};
    EXPECT_THROW(external.since(a), acoustic::Error);
}

TEST(Time, AdvancePreservesClock) {
    const acoustic::Timestamp t{100, {acoustic::ClockDomain::External, 9}};
    const auto advanced = t.advancedBy(acoustic::Duration{25});
    EXPECT_EQ(advanced.nanoseconds(), 125);
    EXPECT_EQ(advanced.clock().source_id, 9U);
}
