#include <audition/temporal/interval_relations.hpp>
#include <gtest/gtest.h>
#include <array>
#include <stdexcept>

namespace {
audition::temporal::EventInterval span(int a, int b, unsigned clock=1) {
    auto id = audition::ClockIdentity{audition::ClockDomain::Simulation, clock};
    return {{a,id},{b,id}};
}
} // namespace

TEST(TemporalIntervalRelations, AllThirteenAllenRelations) {
    using audition::temporal::IntervalRelation;
    const auto pivot = span(10, 20);
    const std::array<std::pair<audition::temporal::EventInterval, IntervalRelation>,13> cases{{
        {span(1,5), IntervalRelation::Before},
        {span(1,10), IntervalRelation::Meets},
        {span(5,15), IntervalRelation::Overlaps},
        {span(10,15), IntervalRelation::Starts},
        {span(12,18), IntervalRelation::During},
        {span(15,20), IntervalRelation::Finishes},
        {span(10,20), IntervalRelation::Equal},
        {span(5,20), IntervalRelation::FinishedBy},
        {span(5,25), IntervalRelation::Contains},
        {span(10,25), IntervalRelation::StartedBy},
        {span(15,25), IntervalRelation::OverlappedBy},
        {span(20,25), IntervalRelation::MetBy},
        {span(25,30), IntervalRelation::After}
    }};
    for (const auto& [interval, expected] : cases) {
        EXPECT_EQ(audition::temporal::relate(interval,pivot),expected);
    }
}

TEST(TemporalIntervalRelations, InvalidClocksAndReversedBoundsRejected) {
    EXPECT_THROW(audition::temporal::relate(span(2,1),span(3,4)),std::invalid_argument);
    EXPECT_THROW(audition::temporal::relate(span(1,2,1),span(1,2,2)),std::invalid_argument);
    EXPECT_EQ(audition::temporal::relate(span(5,5),span(5,5)),
              audition::temporal::IntervalRelation::Equal);
}
