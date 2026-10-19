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

TEST(DynamicTimeWarping, WindowComparisonMatchesCopiedSequenceOracle) {
    // Compare offset views to the original independent copy-based API.
    // Vary dimensions, window offsets, radii and both reachable and
    // unreachable candidate alignments.
    for (const std::size_t radius : {std::size_t{0},std::size_t{1},std::size_t{5}}) {
        audition::temporal::DtwOptions options{};
        options.max_frames=12;
        options.max_dimensions=3;
        options.warping_radius=radius;
        audition::temporal::DynamicTimeWarping dtw{options};
        audition::temporal::DtwWorkspace scratch;
        for (std::size_t dimensions=1;dimensions<=3;++dimensions) {
            Sequence frames(12,std::vector<double>(dimensions));
            for (std::size_t i=0;i<frames.size();++i)
                for (std::size_t d=0;d<dimensions;++d)
                    frames[i][d]=static_cast<double>(((i*7+d*13)%11));
            for (std::size_t length=1;length<=6;++length)
                for (std::size_t a=0;a+length<=frames.size();++a)
                    for (std::size_t b=0;b+length<=frames.size();++b) {
                        const Sequence left(
                            frames.begin()+static_cast<std::ptrdiff_t>(a),
                            frames.begin()+static_cast<std::ptrdiff_t>(a+length));
                        const Sequence right(
                            frames.begin()+static_cast<std::ptrdiff_t>(b),
                            frames.begin()+static_cast<std::ptrdiff_t>(b+length));
                        const auto expected=dtw.compare(left,right);
                        const auto actual=dtw.compareWindows(frames,a,b,length,scratch);
                        ASSERT_EQ(actual.reachable,expected.reachable);
                        EXPECT_DOUBLE_EQ(actual.total_cost,expected.total_cost);
                        EXPECT_DOUBLE_EQ(actual.normalized_cost,expected.normalized_cost);
                        EXPECT_EQ(actual.path_length,expected.path_length);
                    }
        }
    }
}

TEST(DynamicTimeWarping, WindowsValidateBoundsAndArithmeticWithoutPoisoningScratch) {
    audition::temporal::DtwOptions options{};
    options.max_frames=4;
    audition::temporal::DynamicTimeWarping dtw{options};
    audition::temporal::DtwWorkspace workspace;
    const Sequence features{{1},{2},{3},{4}};
    EXPECT_THROW(static_cast<void>(dtw.compareWindows(features,0,1,0,workspace)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(dtw.compareWindows(features,0,1,5,workspace)),
                 std::length_error);
    EXPECT_THROW(static_cast<void>(dtw.compareWindows(features,3,1,2,workspace)),
                 std::out_of_range);
    EXPECT_THROW(static_cast<void>(dtw.compareWindows(features,
        std::numeric_limits<std::size_t>::max(),0,1,workspace)),std::out_of_range);
    const Sequence ragged{{1},{2,3},{1},{2}};
    EXPECT_THROW(static_cast<void>(dtw.compareWindows(ragged,0,1,2,workspace)),
                 std::invalid_argument);
    const Sequence overflow{{std::numeric_limits<double>::max()},
                            {-std::numeric_limits<double>::max()}};
    EXPECT_THROW(static_cast<void>(dtw.compareWindows(overflow,0,1,1,workspace)),
                 std::overflow_error);
    const auto result=dtw.compareWindows(features,0,2,2,workspace);
    EXPECT_TRUE(result.reachable);
    EXPECT_EQ(result.path_length,2u);
    EXPECT_DOUBLE_EQ(result.total_cost,8.0);
}
