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
