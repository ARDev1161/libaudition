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
