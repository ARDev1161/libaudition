#include "../demo/qt/source_activity_gate.hpp"
#include <gtest/gtest.h>

#include <limits>
#include <set>

TEST(SourceActivityGate, InactiveTrackRemainsInvisible) {
    demo::SourceActivityGate gate;
    EXPECT_FALSE(gate.visible(7U, 0.0, 100));
    EXPECT_FALSE(gate.visible(7U, 0.02, 150));
}

TEST(SourceActivityGate, QuietTrackAppearsAndFadesAfterSilence) {
    demo::SourceActivityGate gate;
    EXPECT_TRUE(gate.visible(7U, 0.04, 1000));
    EXPECT_TRUE(gate.visible(7U, 0.0, 1500));
    EXPECT_TRUE(gate.visible(7U, 0.0, 1800));
    EXPECT_FALSE(gate.visible(7U, 0.0, 1801));
    EXPECT_FALSE(gate.visible(7U, 0.0, 2000));
    EXPECT_TRUE(gate.visible(7U, 0.09, 2001));
}

TEST(SourceActivityGate, IntermediateActivityHoldsVisibleSource) {
    demo::SourceActivityGate gate;
    EXPECT_TRUE(gate.visible(7U, 0.2, 10));
    EXPECT_TRUE(gate.visible(7U, 0.015, 500));
    EXPECT_TRUE(gate.visible(7U, 0.0, 1299));
    EXPECT_FALSE(gate.visible(7U, 0.0, 1301));
}

TEST(SourceActivityGate, MissingTrackDoesNotRetainItsPreviousIdentity) {
    demo::SourceActivityGate gate;
    EXPECT_TRUE(gate.visible(9U, 0.7, 10));
    EXPECT_TRUE(gate.visible(10U, 0.2, 10));
    gate.retainOnly(std::set<std::uint64_t>{10U});
    EXPECT_FALSE(gate.visible(9U, 0.0, 11));
    EXPECT_TRUE(gate.visible(10U, 0.0, 11));
    gate.reset();
    EXPECT_FALSE(gate.visible(10U, 0.0, 12));
}

TEST(SourceActivityGate, NonFiniteActivityDoesNotActivateSource) {
    demo::SourceActivityGate gate;
    EXPECT_FALSE(gate.visible(3U, std::numeric_limits<double>::quiet_NaN(), 0));
    EXPECT_FALSE(gate.visible(3U, std::numeric_limits<double>::infinity(), 0));
}
