#include <audition/temporal/streaming_pattern_matcher.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

namespace {
audition::AcousticSourceId source(unsigned value) {return audition::AcousticSourceId{value};}
}
TEST(StreamingPatternMatcher, RecognizesOnlyCompletedPattern) {
    audition::temporal::StreamingPatternMatcher matcher;
    matcher.registerTemplate("beep",{{1},{2},{3}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{2}).empty());
    const auto matches=matcher.push(source(1),{3});
    ASSERT_EQ(matches.size(),1u);
    EXPECT_EQ(matches.front().label,"beep");
    EXPECT_DOUBLE_EQ(matches.front().normalized_cost,0.0);
    EXPECT_EQ(matches.front().ending_frame,3u);
}
TEST(StreamingPatternMatcher, SourcesHaveIndependentHistories) {
    audition::temporal::StreamingPatternMatcher matcher;
    matcher.registerTemplate("beep",{{1},{2}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(2),{2}).empty());
    EXPECT_EQ(matcher.push(source(1),{2}).size(),1u);
    EXPECT_TRUE(matcher.push(source(2),{3}).empty());
}
TEST(StreamingPatternMatcher, ForgetsStalePrefixAndCanRecover) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=3;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("beep",{{1},{2},{3}});
    for(int i=0;i<9;++i) EXPECT_TRUE(matcher.push(source(1),{99}).empty());
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{2}).empty());
    EXPECT_EQ(matcher.push(source(1),{3}).size(),1u);
    matcher.forget(source(1));
    EXPECT_TRUE(matcher.push(source(1),{3}).empty());
}
TEST(StreamingPatternMatcher, RejectsInvalidFramesWithoutPoisoningHistory) {
    audition::temporal::StreamingPatternMatcher matcher;
    matcher.registerTemplate("beep",{{1},{2}});
    (void)matcher.push(source(1),{1});
    EXPECT_THROW(static_cast<void>(matcher.push(source(1),{
        std::numeric_limits<double>::infinity()})),std::invalid_argument);
    EXPECT_EQ(matcher.push(source(1),{2}).size(),1u);
}
TEST(StreamingPatternMatcher, SourceCapacityAndDimensionIsolation) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_sources=1;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("beep",{{1},{2}});
    (void)matcher.push(source(1),{1});
    EXPECT_THROW(static_cast<void>(matcher.push(source(2),{1})),std::length_error);
    EXPECT_THROW(static_cast<void>(matcher.push(source(1),{1,2})),std::invalid_argument);
    EXPECT_EQ(matcher.push(source(1),{2}).size(),1u);
}
TEST(StreamingPatternMatcher, RejectsRaggedAndNonfiniteTemplates) {
    audition::temporal::StreamingPatternMatcher matcher;
    EXPECT_THROW(matcher.registerTemplate("bad",{{1},{2,3}}),std::invalid_argument);
    EXPECT_THROW(matcher.registerTemplate("bad",{{std::numeric_limits<double>::quiet_NaN()}}),std::invalid_argument);
    matcher.registerTemplate("beep",{{1},{2}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
}

TEST(StreamingPatternMatcher, CooldownSuppressesRepeatedAlertsPerSourceAndLabel) {
    audition::temporal::StreamingPatternOptions options{};
    options.cooldown_frames=2;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("steady",{{1}});
    matcher.registerTemplate("other",{{2}});
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
    EXPECT_EQ(matcher.push(source(2),{1}).size(),1u);
    EXPECT_EQ(matcher.push(source(1),{2}).size(),1u);
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
}

TEST(StreamingPatternMatcher, ForgetClearsCooldownAndHistory) {
    audition::temporal::StreamingPatternOptions options{};
    options.cooldown_frames=100;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("steady",{{1}});
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    matcher.forget(source(1));
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
}

TEST(StreamingPatternMatcher, VariableWindowAcceptsDeletedFeatureFrame) {
    audition::temporal::StreamingPatternOptions options{};
    options.min_candidate_frames=2;
    options.max_candidate_frames=3;
    options.warping_radius=2;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("beep",{{1},{2},{3}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_EQ(matcher.push(source(1),{3}).size(),0u);
    // DTW does not magically interpolate the missing middle feature.
    // Repeated feature plateau can be matched by a shorter observation.
    matcher.registerTemplate("plateau",{{1},{1},{3}});
    matcher.forget(source(1));
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    const auto matches=matcher.push(source(1),{3});
    ASSERT_EQ(matches.size(),1u);
    EXPECT_EQ(matches[0].label,"plateau");
}

TEST(StreamingPatternMatcher, FixedWindowDefaultDoesNotAcceptShortObservation) {
    audition::temporal::StreamingPatternMatcher matcher;
    matcher.registerTemplate("plateau",{{1},{1},{3}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{3}).empty());
}

TEST(StreamingPatternMatcher, VariableWindowRejectsInconsistentOptions) {
    audition::temporal::StreamingPatternOptions options{};
    options.min_candidate_frames=5;
    options.max_candidate_frames=3;
    EXPECT_THROW(static_cast<void>(audition::temporal::StreamingPatternMatcher{options}),
                 std::invalid_argument);
}

TEST(StreamingPatternMatcher, MissingMiddleFrameWithoutEquivalentPlateauDoesNotMatch) {
    audition::temporal::StreamingPatternOptions options{};
    options.min_candidate_frames=2;
    options.max_candidate_frames=3;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("three_step",{{1},{2},{3}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{3}).empty());
    // A complete later occurrence still recovers.
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{2}).empty());
    const auto restored=matcher.push(source(1),{3});
    ASSERT_EQ(restored.size(),1u);
    EXPECT_EQ(restored.front().label,"three_step");
    EXPECT_EQ(restored.front().ending_frame,5u);
}

TEST(StreamingPatternMatcher, RollingHistoryBoundAndSourceRestart) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=3;
    options.min_candidate_frames=2;
    options.max_candidate_frames=3;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("cycle",{{4},{5},{6}});
    for (int i=0;i<100;++i)
        EXPECT_TRUE(matcher.push(source(1),{100}).empty());
    EXPECT_TRUE(matcher.push(source(1),{4}).empty());
    EXPECT_TRUE(matcher.push(source(1),{5}).empty());
    const auto found=matcher.push(source(1),{6});
    ASSERT_EQ(found.size(),1u);
    EXPECT_EQ(found.front().ending_frame,103u);
    matcher.forget(source(1));
    EXPECT_TRUE(matcher.push(source(1),{6}).empty());
    EXPECT_TRUE(matcher.push(source(1),{4}).empty());
    EXPECT_TRUE(matcher.push(source(1),{5}).empty());
    EXPECT_EQ(matcher.push(source(1),{6}).size(),1u);
}

TEST(StreamingPatternMatcher, CandidateWindowCannotExceedConfiguredHistory) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=3;
    options.max_candidate_frames=4;
    EXPECT_THROW(static_cast<void>(audition::temporal::StreamingPatternMatcher{options}),
                 std::invalid_argument);
}

TEST(StreamingPatternMatcher, MultipleTemplatesRetainIndependentCooldown) {
    audition::temporal::StreamingPatternOptions options{};
    options.cooldown_frames=10;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("one",{{1}});
    matcher.registerTemplate("two",{{2}});
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
    EXPECT_EQ(matcher.push(source(1),{2}).size(),1u);
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{2}).empty());
}
