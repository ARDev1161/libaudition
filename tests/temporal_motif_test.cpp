#include <audition/temporal/acoustic_motif_finder.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Frames=std::vector<std::vector<double>>;
audition::temporal::MotifOptions options() {
    audition::temporal::MotifOptions o{};
    o.window_frames=3;
    o.max_normalized_cost=0.001;
    return o;
}
}
TEST(AcousticMotifFinder, FindsSeparatedRepeatedPattern) {
    audition::temporal::AcousticMotifFinder finder{options()};
    const auto result=finder.find(Frames{{1},{2},{3},{90},{91},{92},{1},{2},{3}});
    EXPECT_TRUE(result.found);
    EXPECT_EQ(result.first_start,0u);
    EXPECT_EQ(result.second_start,6u);
    EXPECT_DOUBLE_EQ(result.normalized_cost,0.0);
}
TEST(AcousticMotifFinder, NoFalseMatchOnDistantWindows) {
    audition::temporal::AcousticMotifFinder finder{options()};
    EXPECT_FALSE(finder.find(Frames{{0},{1},{2},{100},{101},{102}}).found);
}
TEST(AcousticMotifFinder, ShortHistoryDoesNotEmit) {
    audition::temporal::AcousticMotifFinder finder{options()};
    EXPECT_FALSE(finder.find(Frames{{1},{2},{3},{1},{2}}).found);
}
TEST(AcousticMotifFinder, RejectsNonfiniteAndRaggedFeatureFrames) {
    audition::temporal::AcousticMotifFinder finder{options()};
    EXPECT_THROW(static_cast<void>(finder.find(Frames{{1},{1,2}})),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(finder.find(Frames{{std::numeric_limits<double>::infinity()}})),std::invalid_argument);
}
TEST(AcousticMotifFinder, EnforcesInputCapacity) {
    auto o=options();
    o.max_frames=6;
    audition::temporal::AcousticMotifFinder finder{o};
    EXPECT_THROW(static_cast<void>(finder.find(Frames(7,std::vector<double>{1}))),std::length_error);
}
TEST(AcousticMotifFinder, RejectsInvalidConfiguration) {
    auto o=options();
    o.stride=0;
    EXPECT_THROW(static_cast<void>(audition::temporal::AcousticMotifFinder{o}),std::invalid_argument);
}

TEST(AcousticMotifFinder, MultipleDisjointRepeats) {
    auto o=options();
    o.window_frames=2;
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames data{{1},{2},{1},{2},{7},{8},{7},{8}};
    const auto matches=finder.findMultiple(data,2);
    ASSERT_EQ(matches.size(),2u);
    EXPECT_EQ(matches[0].first_start,0u);
    EXPECT_EQ(matches[0].second_start,2u);
    EXPECT_EQ(matches[1].first_start,4u);
    EXPECT_EQ(matches[1].second_start,6u);
}

TEST(AcousticMotifFinder, MultipleDoesNotReuseOverlappingWindows) {
    auto o=options();
    o.window_frames=2;
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames data(8,std::vector<double>{1});
    const auto matches=finder.findMultiple(data,8);
    EXPECT_LE(matches.size(),2u);
    for (std::size_t i=0;i<matches.size();++i)
        for (std::size_t j=i+1;j<matches.size();++j)
            for (const auto a:{matches[i].first_start,matches[i].second_start})
                for (const auto b:{matches[j].first_start,matches[j].second_start})
                    EXPECT_GE(a>b?a-b:b-a,2u);
}

TEST(AcousticMotifFinder, MultipleHonorsResultCapacityAndValidation) {
    auto o=options();
    o.window_frames=2;
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames data(8,std::vector<double>{1});
    EXPECT_EQ(finder.findMultiple(data,1).size(),1u);
    EXPECT_THROW(static_cast<void>(finder.findMultiple(data,0)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(finder.findMultiple(Frames{{1},{1,2}},1)),
                 std::invalid_argument);
}

TEST(AcousticMotifFinder, ExhaustiveBinaryWindowOracle) {
    // 2^8 complete feature streams. At radius=0/cost=0 a two-frame
    // window matches iff both features match exactly. The oracle uses
    // direct equality and a frame-occupancy array, not DTW or motif code.
    auto o=options();
    o.max_frames=8;
    o.window_frames=2;
    o.warping_radius=0;
    o.max_normalized_cost=0;
    o.stride=1;
    audition::temporal::AcousticMotifFinder finder{o};
    constexpr std::size_t frame_count=8;
    for (unsigned mask=0;mask<256u;++mask) {
        Frames frames(frame_count,std::vector<double>(1));
        for (std::size_t i=0;i<frame_count;++i)
            frames[i][0]=static_cast<double>((mask>>i)&1u);
        std::vector<std::pair<std::size_t,std::size_t>> candidates;
        for (std::size_t a=0;a+3<frame_count;++a)
            for (std::size_t b=a+2;b+1<frame_count;++b)
                if (frames[a][0]==frames[b][0] &&
                    frames[a+1][0]==frames[b+1][0])
                    candidates.emplace_back(a,b);

        const auto best=finder.find(frames);
        ASSERT_EQ(best.found,!candidates.empty()) << "mask="<<mask;
        if (!candidates.empty()) {
            EXPECT_EQ(best.first_start,candidates.front().first) << "mask="<<mask;
            EXPECT_EQ(best.second_start,candidates.front().second) << "mask="<<mask;
            EXPECT_DOUBLE_EQ(best.normalized_cost,0);
        }

        std::array<bool,frame_count> used{};
        std::vector<std::pair<std::size_t,std::size_t>> expected;
        for (const auto& pair:candidates) {
            const auto a=pair.first;
            const auto b=pair.second;
            if (used[a] || used[a+1] || used[b] || used[b+1]) continue;
            used[a]=used[a+1]=used[b]=used[b+1]=true;
            expected.push_back(pair);
            if (expected.size()==2) break;
        }
        const auto found=finder.findMultiple(frames,2);
        ASSERT_EQ(found.size(),expected.size()) << "mask="<<mask;
        for (std::size_t i=0;i<expected.size();++i) {
            EXPECT_EQ(found[i].first_start,expected[i].first) << "mask="<<mask;
            EXPECT_EQ(found[i].second_start,expected[i].second) << "mask="<<mask;
            EXPECT_DOUBLE_EQ(found[i].normalized_cost,0);
            EXPECT_TRUE(found[i].found);
        }
    }
}

TEST(AcousticMotifFinder, HugeStrideDoesNotWrapOrHang) {
    auto o=options();
    o.window_frames=2;
    o.stride=std::numeric_limits<std::size_t>::max();
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames data{{1},{2},{1},{2},{99},{100}};
    const auto single=finder.find(data);
    ASSERT_TRUE(single.found);
    EXPECT_EQ(single.first_start,0u);
    EXPECT_EQ(single.second_start,2u);
    const auto multiple=finder.findMultiple(data,1);
    ASSERT_EQ(multiple.size(),1u);
    EXPECT_EQ(multiple.front().first_start,single.first_start);
    EXPECT_EQ(multiple.front().second_start,single.second_start);
}

TEST(AcousticMotifFinder, HugeWindowDoesNotOverflowShortInputCheck) {
    auto o=options();
    o.max_frames=std::numeric_limits<std::size_t>::max();
    o.window_frames=o.max_frames/2+1;
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames short_data{{1},{2},{3},{4}};
    EXPECT_FALSE(finder.find(short_data).found);
    EXPECT_TRUE(finder.findMultiple(short_data,1).empty());
}

TEST(AcousticMotifFinder, ReusedWindowsPreserveMultidimensionalMatchesAcrossCalls) {
    auto o=options();
    o.max_frames=16;
    o.max_dimensions=4;
    o.window_frames=3;
    o.max_normalized_cost=0;
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames repeated{{1,10,100},{2,20,200},{3,30,300},
                          {80,81,82},{1,10,100},{2,20,200},{3,30,300}};
    const Frames unrelated{{1,2,3},{2,3,4},{3,4,5},
                           {90,91,92},{80,81,82},{70,71,72}};
    for (int iteration=0;iteration<20;++iteration) {
        const auto best=finder.find(repeated);
        ASSERT_TRUE(best.found);
        EXPECT_EQ(best.first_start,0u);
        EXPECT_EQ(best.second_start,4u);
        EXPECT_DOUBLE_EQ(best.normalized_cost,0);
        const auto matches=finder.findMultiple(repeated,3);
        ASSERT_EQ(matches.size(),1u);
        EXPECT_EQ(matches.front().first_start,0u);
        EXPECT_EQ(matches.front().second_start,4u);
        EXPECT_FALSE(finder.find(unrelated).found);
        EXPECT_TRUE(finder.findMultiple(unrelated,2).empty());
    }
}

TEST(AcousticMotifFinder, SingleResultUsesSameOrderingAndThresholdAsFind) {
    auto o=options();
    o.max_frames=64;
    o.window_frames=2;
    o.max_normalized_cost=0;
    audition::temporal::AcousticMotifFinder finder{o};
    // Every pair is a candidate: the single-result API must select the
    // deterministic earliest pair without retaining the quadratic list.
    const Frames repeated(64,std::vector<double>{1});
    const auto best=finder.find(repeated);
    ASSERT_TRUE(best.found);
    const auto one=finder.findMultiple(repeated,1);
    ASSERT_EQ(one.size(),1u);
    EXPECT_EQ(one[0].first_start,best.first_start);
    EXPECT_EQ(one[0].second_start,best.second_start);
    EXPECT_DOUBLE_EQ(one[0].normalized_cost,best.normalized_cost);
    EXPECT_EQ(one[0].first_start,0u);
    EXPECT_EQ(one[0].second_start,2u);

    const Frames distinct{{0},{1},{2},{3},{4},{5}};
    EXPECT_FALSE(finder.find(distinct).found);
    EXPECT_TRUE(finder.findMultiple(distinct,1).empty());
    EXPECT_THROW(static_cast<void>(finder.findMultiple(Frames{{1},{1,2}},1)),
                 std::invalid_argument);
}

TEST(AcousticMotifFinder, PrevalidatedFastPathStillRejectsInvalidMiddleFrames) {
    auto o=options();
    o.window_frames=2;
    o.max_frames=8;
    o.max_dimensions=3;
    o.max_normalized_cost=0;
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames correct{{1,2},{3,4},{9,9},{8,8},{1,2},{3,4}};
    const auto baseline=finder.find(correct);
    ASSERT_TRUE(baseline.found);
    EXPECT_EQ(baseline.first_start,0u);
    EXPECT_EQ(baseline.second_start,4u);
    EXPECT_EQ(finder.findMultiple(correct,2).size(),1u);

    Frames invalid=correct;
    invalid[3].push_back(7);
    EXPECT_THROW(static_cast<void>(finder.find(invalid)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(finder.findMultiple(invalid,2)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(finder.findMultiple(invalid,1)),std::invalid_argument);

    invalid=correct;
    invalid[3][1]=std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(finder.find(invalid)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(finder.findMultiple(invalid,2)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(finder.findMultiple(invalid,1)),std::invalid_argument);

    // Invalid calls must not poison local scratch or subsequent invocations.
    const auto recovered=finder.find(correct);
    ASSERT_TRUE(recovered.found);
    EXPECT_EQ(recovered.first_start,baseline.first_start);
    EXPECT_EQ(recovered.second_start,baseline.second_start);
}

TEST(AcousticMotifFinder, BoundedTwoPairSelectionAgreesWithGeneralGreedyOrder) {
    auto o=options();
    o.max_frames=12;
    o.window_frames=2;
    o.warping_radius=0;
    o.max_normalized_cost=0;
    audition::temporal::AcousticMotifFinder finder{o};

    for (unsigned mask=0;mask<1024u;++mask) {
        Frames frames(12,std::vector<double>(1));
        for (std::size_t i=0;i<frames.size();++i)
            frames[i][0]=static_cast<double>((mask>>(i%10u))&1u);
        // max_matches=3 takes the original global sort/greedy path.
        const auto general=finder.findMultiple(frames,3);
        const auto bounded=finder.findMultiple(frames,2);
        ASSERT_EQ(bounded.size(),std::min(std::size_t{2},general.size()))
            << "mask="<<mask;
        for (std::size_t i=0;i<bounded.size();++i) {
            EXPECT_EQ(bounded[i].first_start,general[i].first_start)
                << "mask="<<mask;
            EXPECT_EQ(bounded[i].second_start,general[i].second_start)
                << "mask="<<mask;
            EXPECT_DOUBLE_EQ(bounded[i].normalized_cost,general[i].normalized_cost)
                << "mask="<<mask;
            EXPECT_TRUE(bounded[i].found);
        }
    }
}

TEST(AcousticMotifFinder, BoundedTwoPairSelectionHandlesLargeStrideAndCapacity) {
    auto o=options();
    o.window_frames=2;
    o.max_frames=12;
    o.stride=std::numeric_limits<std::size_t>::max();
    o.max_normalized_cost=0;
    audition::temporal::AcousticMotifFinder finder{o};
    const Frames data{{1},{2},{1},{2},{1},{2},{1},{2}};
    const auto one=finder.find(data);
    const auto two=finder.findMultiple(data,2);
    ASSERT_TRUE(one.found);
    ASSERT_EQ(two.size(),1u);
    EXPECT_EQ(two.front().first_start,one.first_start);
    EXPECT_EQ(two.front().second_start,one.second_start);
    EXPECT_THROW(static_cast<void>(finder.findMultiple(Frames(13,std::vector<double>{1}),2)),
                 std::length_error);
}

TEST(AcousticMotifFinder, MultipleCandidateBudgetBoundsTemporaryStorage) {
    auto o=options();
    o.max_frames=8;
    o.window_frames=2;
    o.warping_radius=0;
    o.max_normalized_cost=0;
    o.max_candidate_pairs=14;
    const Frames repeated(8,std::vector<double>{1});
    audition::temporal::AcousticMotifFinder limited{o};
    // Seven starts yield 15 non-overlapping candidate pairs.
    // Request >2 results to exercise the materialized-candidate path.
    EXPECT_THROW(static_cast<void>(limited.findMultiple(repeated,3)),
                 std::length_error);
    EXPECT_TRUE(limited.find(repeated).found);
    EXPECT_EQ(limited.findMultiple(repeated,1).size(),1u);
    EXPECT_EQ(limited.findMultiple(repeated,2).size(),2u);

    o.max_candidate_pairs=15;
    audition::temporal::AcousticMotifFinder sufficient{o};
    const auto matches=sufficient.findMultiple(repeated,3);
    ASSERT_EQ(matches.size(),2u);
    EXPECT_EQ(matches[0].first_start,0u);
    EXPECT_EQ(matches[0].second_start,2u);
    EXPECT_EQ(matches[1].first_start,4u);
    EXPECT_EQ(matches[1].second_start,6u);

    // Only accepted candidates count against the budget.
    const Frames distinct{{0},{1},{2},{3},{4},{5},{6},{7}};
    o.max_candidate_pairs=1;
    audition::temporal::AcousticMotifFinder sparse{o};
    EXPECT_TRUE(sparse.findMultiple(distinct,3).empty());

    o.max_candidate_pairs=0;
    EXPECT_THROW(static_cast<void>(audition::temporal::AcousticMotifFinder{o}),
                 std::invalid_argument);
}
