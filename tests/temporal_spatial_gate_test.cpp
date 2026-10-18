#include <audition/temporal/spatial_temporal_gate.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
audition::temporal::SpatialGateObservation point(std::int64_t t, double x,
                                                  unsigned clock = 1) {
    return {{t,{audition::ClockDomain::Simulation,clock}},x,0.0,0.0};
}
}

TEST(SpatialTemporalGate, CompatibleAtConfiguredBoundaries) {
    audition::temporal::SpatialGateOptions o{};
    o.max_elapsed = audition::Duration{10};
    o.max_distance_m = 2;
    audition::temporal::SpatialTemporalGate gate(o);
    auto r = gate.evaluate(point(0,0),point(10,2));
    EXPECT_TRUE(r.compatible);
    EXPECT_DOUBLE_EQ(r.distance_m,2);
    EXPECT_EQ(r.elapsed_nanoseconds,10u);
}
TEST(SpatialTemporalGate, RejectsSpatialAndTemporalOutliers) {
    audition::temporal::SpatialGateOptions o{};
    o.max_elapsed = audition::Duration{5};
    o.max_distance_m = 1;
    audition::temporal::SpatialTemporalGate gate(o);
    EXPECT_FALSE(gate.evaluate(point(0,0),point(3,2)).compatible);
    auto late=gate.evaluate(point(0,0),point(6,0));
    EXPECT_FALSE(late.compatible);
    EXPECT_EQ(late.elapsed_nanoseconds,6u);
    EXPECT_TRUE(gate.evaluate(point(0,0),point(5,1)).compatible);
}
TEST(SpatialTemporalGate, OppositeSignedExtremeTimestampsRemainExact) {
    audition::temporal::SpatialTemporalGate gate;
    const auto r=gate.evaluate(point(std::numeric_limits<std::int64_t>::min(),0),
                               point(std::numeric_limits<std::int64_t>::max(),0));
    EXPECT_FALSE(r.compatible);
    EXPECT_EQ(r.elapsed_nanoseconds,std::numeric_limits<std::uint64_t>::max());
}
TEST(SpatialTemporalGate, RejectsClockMismatchNonfiniteGeometryAndOptions) {
    audition::temporal::SpatialTemporalGate gate;
    EXPECT_THROW(static_cast<void>(gate.evaluate(point(0,0,1),point(0,0,2))),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(gate.evaluate(point(0,0),
        point(0,std::numeric_limits<double>::infinity()))),std::invalid_argument);
    audition::temporal::SpatialGateOptions o{};
    o.max_elapsed=audition::Duration{-1};
    EXPECT_THROW(audition::temporal::SpatialTemporalGate{o},std::invalid_argument);
}
