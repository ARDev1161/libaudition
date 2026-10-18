#include <audition/temporal/event_episode_associator.hpp>
#include <audition/temporal/hungarian_event_matcher.hpp>
#include <audition/temporal/mahalanobis_gate.hpp>
#include <gtest/gtest.h>
#include <optional>
#include <vector>
#include <cstdint>

namespace {
using namespace audition::temporal;
audition::Timestamp stamp(int n) {
    return {n,{audition::ClockDomain::Simulation,1}};
}
DiagonalPositionEvidence point(int t, double x, double var=0.1) {
    return {stamp(t), {x,0,0}, {var,var,var}};
}
std::vector<std::vector<std::optional<double>>> buildCosts(
    const std::vector<DiagonalPositionEvidence>& observations,
    const std::vector<DiagonalPositionEvidence>& tracks,
    const DiagonalMahalanobisGate& gate) {
    std::vector<std::vector<std::optional<double>>> costs;
    for (const auto& observation : observations) {
        auto& row = costs.emplace_back();
        for (const auto& track : tracks) {
            const auto evidence=gate.evaluate(observation,track);
            if(evidence.compatible) row.emplace_back(evidence.squared_distance);
            else row.emplace_back(std::nullopt);
        }
    }
    return costs;
}
}
TEST(TemporalAssociationScenario, CrossingAssignmentsMinimizeGlobalCost) {
    MahalanobisGateOptions options{};
    options.max_elapsed=audition::Duration{10};
    options.max_squared_distance=50;
    DiagonalMahalanobisGate gate(options);
    HungarianEventMatcher matcher;
    // Track columns are spatially ordered right-to-left, not by ID.
    const auto costs=buildCosts({point(2,1),point(2,9)},
                                {point(1,10),point(1,0)},gate);
    const auto match=matcher.match(costs,2);
    ASSERT_EQ(match.track_for_observation.size(),2u);
    EXPECT_EQ(match.track_for_observation[0],std::optional<std::size_t>{1});
    EXPECT_EQ(match.track_for_observation[1],std::optional<std::size_t>{0});
}
TEST(TemporalAssociationScenario, DisappearedSourceRemainsUnmatched) {
    MahalanobisGateOptions options{};
    options.max_elapsed=audition::Duration{3};
    options.max_squared_distance=9;
    DiagonalMahalanobisGate gate(options);
    HungarianEventMatcher matcher;
    auto match=matcher.match(buildCosts({point(2,0.1)},
                                         {point(1,0),point(1,20)},gate),2);
    ASSERT_EQ(match.track_for_observation.size(),1u);
    EXPECT_EQ(match.track_for_observation[0],std::optional<std::size_t>{0});
    EXPECT_EQ(match.unmatched_tracks,std::vector<std::size_t>{1});
}
TEST(TemporalAssociationScenario, NewDistantObservationCannotStealTrack) {
    MahalanobisGateOptions options{};
    options.max_elapsed=audition::Duration{3};
    options.max_squared_distance=9;
    DiagonalMahalanobisGate gate(options);
    HungarianEventMatcher matcher;
    const auto match=matcher.match(buildCosts({point(2,100),point(2,0.1)},
                                              {point(1,0)},gate),1);
    EXPECT_FALSE(match.track_for_observation[0].has_value());
    EXPECT_EQ(match.track_for_observation[1],std::optional<std::size_t>{0});
}
TEST(TemporalAssociationScenario, RecycledExternalTrackDoesNotReuseEpisode) {
    EventEpisodeAssociator episodes;
    const audition::AcousticSourceId old_source{42},new_source{43};
    auto old=episodes.observe({old_source,"speech",stamp(1),true});
    ASSERT_EQ(old.size(),1u);
    auto closed=episodes.retire(old_source);
    ASSERT_EQ(closed.size(),1u);
    // External spatial track number may be recycled. Resolver issues a new
    // persistent acoustic source; the associator never uses the track number.
    auto fresh=episodes.observe({new_source,"speech",stamp(2),true});
    ASSERT_EQ(fresh.size(),1u);
    EXPECT_NE(old[0].episode_id,fresh[0].episode_id);
    EXPECT_EQ(closed[0].episode_id,old[0].episode_id);
}
