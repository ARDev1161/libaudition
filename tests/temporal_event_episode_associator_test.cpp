#include <audition/temporal/event_episode_associator.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
namespace {
audition::Timestamp ts(int t, unsigned clock=1) {
    return {t,{audition::ClockDomain::Simulation,clock}};
}
audition::temporal::AssociationObservation observation(
    unsigned id, int t, bool active, const char* label="alarm") {
    return {audition::AcousticSourceId{id},label,ts(t),active};
}
}
TEST(EventEpisodeAssociator, RepeatedEvidenceMaintainsEpisodeAndEndUsesSupportedTime) {
    audition::temporal::EventEpisodeAssociator a;
    auto first=a.observe(observation(1,1,true));
    ASSERT_EQ(first.size(),1u);
    auto continued=a.observe(observation(1,2,true));
    ASSERT_EQ(continued.size(),1u);
    EXPECT_EQ(continued[0].kind,audition::temporal::AssociationChangeKind::Continued);
    EXPECT_EQ(continued[0].episode_id,first[0].episode_id);
    auto ended=a.observe(observation(1,3,false));
    ASSERT_EQ(ended.size(),1u);
    EXPECT_EQ(ended[0].episode_id,first[0].episode_id);
    EXPECT_EQ(ended[0].last_supported.nanoseconds(),2);
}
TEST(EventEpisodeAssociator, SameLabelsDoNotMergeDifferentSources) {
    audition::temporal::EventEpisodeAssociator a;
    auto one=a.observe(observation(1,1,true));
    auto two=a.observe(observation(2,1,true));
    EXPECT_NE(one[0].episode_id,two[0].episode_id);
    EXPECT_EQ(a.sourceCount(),2u);
}
TEST(EventEpisodeAssociator, GapClosesAndReopensWithDistinctId) {
    audition::temporal::EventAssociationOptions options{};
    options.max_gap=audition::Duration{2};
    audition::temporal::EventEpisodeAssociator a(options);
    auto old=a.observe(observation(1,1,true));
    auto next=a.observe(observation(1,4,true));
    ASSERT_EQ(next.size(),2u);
    EXPECT_EQ(next[0].kind,audition::temporal::AssociationChangeKind::Ended);
    EXPECT_EQ(next[1].kind,audition::temporal::AssociationChangeKind::Started);
    EXPECT_EQ(next[0].episode_id,old[0].episode_id);
    EXPECT_NE(next[1].episode_id,old[0].episode_id);
}
TEST(EventEpisodeAssociator, ForgetDoesNotReuseIdAfterTrackRecycling) {
    audition::temporal::EventEpisodeAssociator a;
    auto first=a.observe(observation(1,1,true));
    a.forget(audition::AcousticSourceId{1});
    auto second=a.observe(observation(1,1,true));
    EXPECT_NE(first[0].episode_id,second[0].episode_id);
    a.reset();
    auto third=a.observe(observation(1,1,true));
    EXPECT_NE(second[0].episode_id,third[0].episode_id);
}
TEST(EventEpisodeAssociator, InvalidTimeAndCapacityAreTransactional) {
    audition::temporal::EventAssociationOptions options{};
    options.max_sources=1;
    options.max_labels_per_source=1;
    audition::temporal::EventEpisodeAssociator a(options);
    (void)a.observe(observation(1,1,true));
    EXPECT_THROW(static_cast<void>(a.observe(observation(2,1,true))),std::length_error);
    EXPECT_THROW(static_cast<void>(a.observe(observation(1,2,true,"other"))),std::length_error);
    EXPECT_THROW(static_cast<void>(a.observe(observation(1,1,false))),std::invalid_argument);
    auto wrong=observation(1,2,true);
    wrong.time=ts(2,2);
    EXPECT_THROW(static_cast<void>(a.observe(wrong)),std::invalid_argument);
    auto valid=a.observe(observation(1,2,true));
    EXPECT_EQ(valid[0].kind,audition::temporal::AssociationChangeKind::Continued);
}
TEST(EventEpisodeAssociator, RetireEmitsDeterministicallyOrderedEndings) {
    audition::temporal::EventEpisodeAssociator a;
    (void)a.observe(observation(4,1,true,"z"));
    (void)a.observe(observation(4,1,true,"a"));
    auto closed=a.retire(audition::AcousticSourceId{4});
    ASSERT_EQ(closed.size(),2u);
    EXPECT_EQ(closed[0].label,"a");
    EXPECT_EQ(closed[1].label,"z");
    EXPECT_EQ(a.sourceCount(),0u);
}
