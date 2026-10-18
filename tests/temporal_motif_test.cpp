#include <audition/temporal/acoustic_motif_finder.hpp>
#include <gtest/gtest.h>
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
