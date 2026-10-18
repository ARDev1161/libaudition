#include <audition/temporal/mahalanobis_gate.hpp>
#include <audition/temporal/hungarian_event_matcher.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <stdexcept>
using namespace audition::temporal;
namespace {
DiagonalPositionEvidence position(int time,double x,double variance=1,unsigned clock=1) {
    return {{time,{audition::ClockDomain::Simulation,clock}}, {x,0,0}, {variance,1,1}};
}
}
TEST(MahalanobisGate, AcceptsNearAndRejectsFarWithEqualGeometry) {
    DiagonalMahalanobisGate g;
    auto near=g.evaluate(position(0,0),position(0,1));
    EXPECT_TRUE(near.compatible);
    EXPECT_NEAR(near.squared_distance,0.5,1e-12);
    EXPECT_FALSE(g.evaluate(position(0,0,0.01),position(0,1,0.01)).compatible);
}
TEST(MahalanobisGate, RejectsClockOrInvalidVariance) {
    DiagonalMahalanobisGate g;
    EXPECT_THROW(static_cast<void>(g.evaluate(position(0,0,1,1),position(0,0,1,2))),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(g.evaluate(position(0,0,-1),position(0,0))),std::invalid_argument);
}
TEST(MahalanobisGate, ExactExtremeTimeGap) {
    DiagonalMahalanobisGate g;
    auto a=position(0,0),b=position(0,0);
    a.time={std::numeric_limits<std::int64_t>::min(),a.time.clock()};
    b.time={std::numeric_limits<std::int64_t>::max(),b.time.clock()};
    const auto r=g.evaluate(a,b);
    EXPECT_FALSE(r.compatible);
    EXPECT_EQ(r.elapsed_nanoseconds,std::numeric_limits<std::uint64_t>::max());
}
TEST(HungarianEventMatcher, FindsGlobalOptimumNotGreedy) {
    HungarianEventMatcher m;
    const auto r=m.match({{1.0,2.0},{1.1,9.0}},2);
    ASSERT_EQ(r.track_for_observation.size(),2u);
    EXPECT_EQ(r.track_for_observation[0],std::optional<std::size_t>{1});
    EXPECT_EQ(r.track_for_observation[1],std::optional<std::size_t>{0});
    EXPECT_NEAR(r.total_cost,3.1,1e-12);
    EXPECT_TRUE(r.unmatched_tracks.empty());
}
TEST(HungarianEventMatcher, SupportsForbiddenEdgesAndUnmatchedObservations) {
    HungarianEventMatcher m;
    const auto r=m.match({{std::nullopt,1.0},{std::nullopt,std::nullopt}},2);
    ASSERT_EQ(r.track_for_observation.size(),2u);
    EXPECT_EQ(r.track_for_observation[0],std::optional<std::size_t>{1});
    EXPECT_FALSE(r.track_for_observation[1].has_value());
    EXPECT_EQ(r.unmatched_tracks,std::vector<std::size_t>{0});
    EXPECT_DOUBLE_EQ(r.total_cost,11.0);
}
TEST(HungarianEventMatcher, EmptyAndInputValidation) {
    HungarianEventMatcher m;
    EXPECT_EQ(m.match({},2).unmatched_tracks.size(),2u);
    EXPECT_THROW(static_cast<void>(m.match({{1.0}},2)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(m.match({{-1.0}},1)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(m.match({{std::numeric_limits<double>::infinity()}},1)),std::invalid_argument);
}
