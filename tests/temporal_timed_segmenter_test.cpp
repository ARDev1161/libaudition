#include <audition/temporal/timed_segmenter.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
#include <limits>
namespace {
audition::Timestamp t(int n, unsigned c=1) {
    return {n,{audition::ClockDomain::Simulation,c}};
}
audition::AcousticSourceId id(unsigned n=1) { return audition::AcousticSourceId{n}; }
}
TEST(TimedActivitySegmenter, RequiresElapsedOnTimeAndHangover) {
    audition::temporal::TimedSegmentOptions o{};
    o.min_on = audition::Duration{3};
    o.hangover = audition::Duration{2};
    audition::temporal::TimedActivitySegmenter s(o);
    EXPECT_TRUE(s.observe(id(), t(1), true).empty());
    EXPECT_TRUE(s.observe(id(), t(2), true).empty());
    auto start = s.observe(id(), t(4), true);
    ASSERT_EQ(start.size(),1u);
    EXPECT_EQ(start[0].start.nanoseconds(),1);
    EXPECT_TRUE(s.observe(id(),t(5),false).empty());
    EXPECT_TRUE(s.observe(id(),t(6),true).empty());
    EXPECT_TRUE(s.observe(id(),t(7),false).empty());
    auto end=s.observe(id(),t(8),false);
    ASSERT_EQ(end.size(),1u);
    EXPECT_EQ(end[0].occurrence_id,start[0].occurrence_id);
    EXPECT_EQ(end[0].last_active.nanoseconds(),6);
}
TEST(TimedActivitySegmenter, RejectsShortCandidatesAndRetireClosesConfirmed) {
    audition::temporal::TimedSegmentOptions o{};
    o.min_on=audition::Duration{3};
    audition::temporal::TimedActivitySegmenter s(o);
    EXPECT_TRUE(s.observe(id(),t(1),true).empty());
    EXPECT_TRUE(s.observe(id(),t(2),false).empty());
    EXPECT_TRUE(s.retire(id()).empty());
    EXPECT_TRUE(s.observe(id(),t(5),true).empty());
    auto start=s.observe(id(),t(9),true);
    ASSERT_EQ(start.size(),1u);
    auto close=s.retire(id());
    ASSERT_EQ(close.size(),1u);
    EXPECT_EQ(close[0].occurrence_id,start[0].occurrence_id);
}
TEST(TimedActivitySegmenter, GapTerminatesAndRestarts) {
    audition::temporal::TimedSegmentOptions o{};
    o.max_gap=audition::Duration{3};
    audition::temporal::TimedActivitySegmenter s(o);
    auto start=s.observe(id(),t(1),true);
    auto changes=s.observe(id(),t(6),true);
    ASSERT_EQ(start.size(),1u);
    ASSERT_EQ(changes.size(),2u);
    EXPECT_EQ(changes[0].kind,audition::temporal::SegmentTransitionKind::Ended);
    EXPECT_EQ(changes[1].kind,audition::temporal::SegmentTransitionKind::Started);
    EXPECT_NE(changes[0].occurrence_id,changes[1].occurrence_id);
}
TEST(TimedActivitySegmenter, InvalidClockAndCapacityTransactional) {
    audition::temporal::TimedSegmentOptions o{};
    o.max_sources=1;
    audition::temporal::TimedActivitySegmenter s(o);
    (void)s.observe(id(),t(1),true);
    EXPECT_THROW(static_cast<void>(s.observe(id(2),t(2),true)),std::length_error);
    EXPECT_THROW(static_cast<void>(s.observe(id(),t(2,2),true)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(s.observe(id(),t(1),false)),std::invalid_argument);
    EXPECT_TRUE(s.observe(id(),t(2),true).empty());
}

TEST(TimedActivitySegmenter, FullRangeTimeDifferenceDoesNotOverflow) {
    audition::temporal::TimedSegmentOptions o{};
    o.max_gap = audition::Duration{3};
    o.min_on = audition::Duration{2};
    audition::temporal::TimedActivitySegmenter s(o);
    const auto low=std::numeric_limits<std::int64_t>::min();
    const auto high=std::numeric_limits<std::int64_t>::max();
    auto first=s.observe(id(),audition::Timestamp{low,{audition::ClockDomain::Simulation,1}},true);
    EXPECT_TRUE(first.empty());
    auto resumed=s.observe(id(),audition::Timestamp{high,{audition::ClockDomain::Simulation,1}},true);
    EXPECT_TRUE(resumed.empty()); // gap resets unconfirmed candidate
    auto continued=s.retire(id());
    EXPECT_TRUE(continued.empty());
}
TEST(TimedActivitySegmenter, FullRangeGapClosesConfirmedOccurrence) {
    audition::temporal::TimedSegmentOptions o{};
    o.max_gap=audition::Duration{3};
    audition::temporal::TimedActivitySegmenter s(o);
    const auto low=std::numeric_limits<std::int64_t>::min();
    const auto high=std::numeric_limits<std::int64_t>::max();
    auto first=s.observe(id(),audition::Timestamp{low,{audition::ClockDomain::Simulation,1}},true);
    ASSERT_EQ(first.size(),1u);
    auto next=s.observe(id(),audition::Timestamp{high,{audition::ClockDomain::Simulation,1}},false);
    ASSERT_EQ(next.size(),1u);
    EXPECT_EQ(next.front().kind,audition::temporal::SegmentTransitionKind::Ended);
    EXPECT_EQ(next.front().last_active.nanoseconds(),low);
}
