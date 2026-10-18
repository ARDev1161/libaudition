#include <audition/temporal/dynamic_time_warping.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Sequence=std::vector<std::vector<double>>;
}
TEST(DynamicTimeWarping, IdenticalSequencesHaveZeroCost) {
    audition::temporal::DynamicTimeWarping dtw;
    const Sequence seq{{0,1},{1,2},{2,3}};
    const auto result=dtw.compare(seq,seq);
    EXPECT_TRUE(result.reachable);
    EXPECT_DOUBLE_EQ(result.total_cost,0.0);
    EXPECT_EQ(result.path_length,3u);
}
TEST(DynamicTimeWarping, StretchedSequenceAlignsExactly) {
    audition::temporal::DynamicTimeWarping dtw;
    const auto result=dtw.compare(Sequence{{0},{1},{2}},Sequence{{0},{0},{1},{2}});
    EXPECT_TRUE(result.reachable);
    EXPECT_DOUBLE_EQ(result.total_cost,0.0);
    EXPECT_GE(result.path_length,4u);
}
TEST(DynamicTimeWarping, NarrowBandCanMakeAlignmentUnreachable) {
    audition::temporal::DtwOptions options{};
    options.warping_radius=0;
    audition::temporal::DynamicTimeWarping dtw{options};
    const auto result=dtw.compare(Sequence{{0},{1}},Sequence{{0},{1},{2}});
    EXPECT_FALSE(result.reachable);
}
TEST(DynamicTimeWarping, RejectsMalformedOrNonfiniteFrames) {
    audition::temporal::DynamicTimeWarping dtw;
    EXPECT_THROW(static_cast<void>(dtw.compare({},Sequence{{1}})),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(dtw.compare(Sequence{{1,2}},Sequence{{1}})),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(dtw.compare(Sequence{{1}},Sequence{{std::numeric_limits<double>::infinity()}})),std::invalid_argument);
}
TEST(DynamicTimeWarping, RejectsArithmeticOverflow) {
    audition::temporal::DynamicTimeWarping dtw;
    const double huge=std::numeric_limits<double>::max();
    EXPECT_THROW(static_cast<void>(dtw.compare(Sequence{{huge}},Sequence{{-huge}})),std::overflow_error);
}
TEST(DynamicTimeWarping, EnforcesFrameCapacity) {
    audition::temporal::DtwOptions options{};
    options.max_frames=2;
    audition::temporal::DynamicTimeWarping dtw{options};
    EXPECT_THROW(static_cast<void>(dtw.compare(Sequence{{0},{1},{2}},Sequence{{1}})),std::length_error);
}

TEST(DynamicTimeWarping, ReusedWorkspaceMatchesStatelessAcrossDifferentLengths) {
    audition::temporal::DynamicTimeWarping dtw;
    audition::temporal::DtwWorkspace workspace;
    const Sequence a{{0},{1},{2}};
    for (const Sequence& b : {Sequence{{0},{0},{1},{2}},
                             Sequence{{0},{1},{2}},
                             Sequence{{9},{8}}}) {
        const auto expected=dtw.compare(a,b);
        const auto actual=dtw.compare(a,b,workspace);
        EXPECT_EQ(actual.reachable,expected.reachable);
        EXPECT_DOUBLE_EQ(actual.total_cost,expected.total_cost);
        EXPECT_DOUBLE_EQ(actual.normalized_cost,expected.normalized_cost);
        EXPECT_EQ(actual.path_length,expected.path_length);
    }
}
TEST(DynamicTimeWarping, ReusedWorkspaceSurvivesRejectedInput) {
    audition::temporal::DynamicTimeWarping dtw;
    audition::temporal::DtwWorkspace workspace;
    (void)dtw.compare(Sequence{{1}},Sequence{{1}},workspace);
    EXPECT_THROW(static_cast<void>(dtw.compare(Sequence{{1}},
        Sequence{{std::numeric_limits<double>::quiet_NaN()}},workspace)),
        std::invalid_argument);
    const auto result=dtw.compare(Sequence{{1},{2}},Sequence{{1},{2}},workspace);
    EXPECT_TRUE(result.reachable);
    EXPECT_DOUBLE_EQ(result.total_cost,0.0);
}
