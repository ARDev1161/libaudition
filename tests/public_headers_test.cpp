#include <audition/audition.hpp>
#include <audition/interfaces/source_identity.hpp>

#include <gtest/gtest.h>

TEST(PublicHeaders, UmbrellaHeaderCompiles) {
    EXPECT_EQ(audition::kVersionMajor, 1);
    EXPECT_EQ(audition::kVersionMinor, 0);
    EXPECT_EQ(audition::kVersionPatch, 0);
    EXPECT_EQ(audition::kVersion, "1.0.0-rc.1");
}

TEST(PublicHeaders, TemporalAlgorithmsAccessibleFromUmbrella) {
    // Include only <audition/audition.hpp>. A C++17 user must be able to
    // construct backend-neutral temporal components without extra headers.
    audition::temporal::DynamicTimeWarping dtw;
    const std::vector<std::vector<double>> sequence{{1},{2}};
    const auto aligned=dtw.compare(sequence,sequence);
    ASSERT_TRUE(aligned.reachable);
    EXPECT_DOUBLE_EQ(aligned.normalized_cost,0.0);

    audition::temporal::StreamingPatternMatcher streaming;
    streaming.registerTemplate("beep",{{1},{2}});
    EXPECT_TRUE(streaming.push(audition::AcousticSourceId{1},{1}).empty());
    const auto found=streaming.push(audition::AcousticSourceId{1},{2});
    ASSERT_EQ(found.size(),1u);
    EXPECT_EQ(found[0].label,"beep");

    audition::temporal::AcousticMotifFinder motifs;
    audition::temporal::RecurrenceAnalyzer recurrence;
    audition::temporal::TemporalScalarAnalyzer scalar;
    static_cast<void>(motifs);
    static_cast<void>(recurrence);
    static_cast<void>(scalar);
}
