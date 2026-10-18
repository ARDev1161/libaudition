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
