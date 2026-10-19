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

TEST(StreamingPatternMatcher, SharedSuffixCandidatesPreserveMultipleTemplateResults) {
    audition::temporal::StreamingPatternOptions options{};
    options.min_candidate_frames=2;
    options.max_candidate_frames=3;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("first",{{1},{2},{3}});
    matcher.registerTemplate("second",{{1},{1},{3}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{2}).empty());
    const auto matches=matcher.push(source(1),{3});
    ASSERT_EQ(matches.size(),1u);
    EXPECT_EQ(matches.front().label,"first");
    matcher.forget(source(1));
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    const auto shortened=matcher.push(source(1),{3});
    ASSERT_EQ(shortened.size(),1u);
    EXPECT_EQ(shortened.front().label,"second");
}

TEST(StreamingPatternMatcher, FailedDtwArithmeticLeavesHistoryAndCooldownUnchanged) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=3;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("sequence",{{1},{2}});
    matcher.registerTemplate("large",{{1},{std::numeric_limits<double>::max()}});
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_THROW(static_cast<void>(matcher.push(source(1),{-std::numeric_limits<double>::max()})),
                 std::overflow_error);
    // Remove the deliberately overflowing template before retrying. Otherwise
    // the matcher correctly raises another overflow on the recovery frame.
    matcher.registerTemplate("large",{{8},{9}});
    const auto recovered=matcher.push(source(1),{2});
    ASSERT_EQ(recovered.size(),1u);
    EXPECT_EQ(recovered.front().label,"sequence");
    EXPECT_EQ(recovered.front().ending_frame,2u);
}

TEST(StreamingPatternMatcher, UnmatchedFramesPreserveExistingCooldown) {
    audition::temporal::StreamingPatternOptions options{};
    options.cooldown_frames=4;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("pulse",{{1}});
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
    EXPECT_TRUE(matcher.push(source(1),{9}).empty());
    EXPECT_TRUE(matcher.push(source(1),{9}).empty());
    EXPECT_TRUE(matcher.push(source(1),{1}).empty());
    EXPECT_TRUE(matcher.push(source(1),{9}).empty());
    EXPECT_EQ(matcher.push(source(1),{1}).size(),1u);
}

TEST(StreamingPatternMatcher, ReusedDtwScratchKeepsSourcesIndependent) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=5;
    options.min_candidate_frames=2;
    options.max_candidate_frames=4;
    options.warping_radius=2;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("a",{{1},{2},{3}});
    matcher.registerTemplate("b",{{7},{8},{9}});
    for (int repetition=0;repetition<12;++repetition) {
        matcher.forget(source(1));
        matcher.forget(source(2));
        EXPECT_TRUE(matcher.push(source(1),{1}).empty());
        EXPECT_TRUE(matcher.push(source(2),{7}).empty());
        EXPECT_TRUE(matcher.push(source(1),{2}).empty());
        EXPECT_TRUE(matcher.push(source(2),{8}).empty());
        const auto first=matcher.push(source(1),{3});
        ASSERT_EQ(first.size(),1u);
        EXPECT_EQ(first.front().label,"a");
        const auto second=matcher.push(source(2),{9});
        ASSERT_EQ(second.size(),1u);
        EXPECT_EQ(second.front().label,"b");
    }
}

TEST(StreamingPatternMatcher, ReusedSuffixBuffersDoNotLeakPriorSourceFrames) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=5;
    options.min_candidate_frames=2;
    options.max_candidate_frames=4;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("left",{{1},{2},{3}});
    matcher.registerTemplate("right",{{7},{8},{9}});
    for (int iteration=0;iteration<20;++iteration) {
        matcher.forget(source(1));
        matcher.forget(source(2));
        EXPECT_TRUE(matcher.push(source(1),{1}).empty());
        EXPECT_TRUE(matcher.push(source(2),{7}).empty());
        EXPECT_TRUE(matcher.push(source(1),{2}).empty());
        EXPECT_TRUE(matcher.push(source(2),{8}).empty());
        const auto left=matcher.push(source(1),{3});
        ASSERT_EQ(left.size(),1u);
        EXPECT_EQ(left[0].label,"left");
        const auto right=matcher.push(source(2),{9});
        ASSERT_EQ(right.size(),1u);
        EXPECT_EQ(right[0].label,"right");
        EXPECT_TRUE(matcher.push(source(1),{100}).empty());
        EXPECT_TRUE(matcher.push(source(2),{100}).empty());
    }
}

TEST(StreamingPatternMatcher, FullHistoryRecyclesFramesWithoutMixingSources) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=3;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("one",{{1},{2},{3}});
    matcher.registerTemplate("two",{{7},{8},{9}});
    for (int cycle=0;cycle<25;++cycle) {
        EXPECT_TRUE(matcher.push(source(1),{1}).empty());
        EXPECT_TRUE(matcher.push(source(2),{7}).empty());
        EXPECT_TRUE(matcher.push(source(1),{2}).empty());
        EXPECT_TRUE(matcher.push(source(2),{8}).empty());
        const auto a=matcher.push(source(1),{3});
        ASSERT_EQ(a.size(),1u);
        EXPECT_EQ(a.front().label,"one");
        const auto b=matcher.push(source(2),{9});
        ASSERT_EQ(b.size(),1u);
        EXPECT_EQ(b.front().label,"two");
        EXPECT_TRUE(matcher.push(source(1),{100}).empty());
        EXPECT_TRUE(matcher.push(source(2),{100}).empty());
    }
}

TEST(StreamingPatternMatcher, ZeroCooldownEmitsEveryMatchEvenAcrossSources) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=2;
    options.cooldown_frames=0;
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("steady",{{1}});
    matcher.registerTemplate("other",{{2}});
    for (std::size_t frame=1;frame<=25;++frame) {
        const auto first=matcher.push(source(1),{1});
        ASSERT_EQ(first.size(),1u);
        EXPECT_EQ(first.front().label,"steady");
        EXPECT_EQ(first.front().ending_frame,frame);
        const auto second=matcher.push(source(2),{2});
        ASSERT_EQ(second.size(),1u);
        EXPECT_EQ(second.front().label,"other");
        EXPECT_EQ(second.front().ending_frame,frame);
    }
    matcher.forget(source(1));
    const auto restarted=matcher.push(source(1),{1});
    ASSERT_EQ(restarted.size(),1u);
    EXPECT_EQ(restarted.front().ending_frame,1u);
}

TEST(StreamingPatternMatcher, ReusableResultsAgreeWithReturningApiAcrossSourcesAndCooldown) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_frames=4;
    options.max_normalized_cost=0;
    options.cooldown_frames=4;
    audition::temporal::StreamingPatternMatcher returning{options};
    audition::temporal::StreamingPatternMatcher reusable{options};
    for (auto* matcher:{&returning,&reusable}) {
        matcher->registerTemplate("first",{{1},{2},{3}});
        matcher->registerTemplate("second",{{7},{8}});
    }
    std::vector<audition::temporal::StreamingPatternMatch> output;
    output.reserve(options.max_templates);
    const auto initial_capacity=output.capacity();
    for (std::size_t i=0;i<80;++i) {
        const std::vector<double> left{static_cast<double>((i%4)+1)};
        const std::vector<double> right{static_cast<double>(7+(i%3))};
        for (const auto& sample: {std::pair<unsigned,const std::vector<double>*>{1,&left},
                                  std::pair<unsigned,const std::vector<double>*>{2,&right}}) {
            const auto expected=returning.push(source(sample.first),*sample.second);
            reusable.pushInto(source(sample.first),*sample.second,output);
            ASSERT_EQ(output.size(),expected.size());
            for (std::size_t k=0;k<expected.size();++k) {
                EXPECT_EQ(output[k].label,expected[k].label);
                EXPECT_DOUBLE_EQ(output[k].normalized_cost,expected[k].normalized_cost);
                EXPECT_EQ(output[k].ending_frame,expected[k].ending_frame);
            }
            EXPECT_EQ(output.capacity(),initial_capacity);
        }
    }
}

TEST(StreamingPatternMatcher, ReusableResultsPreserveHistoryAfterInvalidFrame) {
    audition::temporal::StreamingPatternOptions options{};
    options.max_normalized_cost=0;
    audition::temporal::StreamingPatternMatcher matcher{options};
    matcher.registerTemplate("beep",{{1},{2}});
    std::vector<audition::temporal::StreamingPatternMatch> output;
    output.reserve(2);
    matcher.pushInto(source(1),{1},output);
    EXPECT_TRUE(output.empty());
    EXPECT_THROW(matcher.pushInto(source(1),
                 {std::numeric_limits<double>::infinity()},output),std::invalid_argument);
    matcher.pushInto(source(1),{2},output);
    ASSERT_EQ(output.size(),1u);
    EXPECT_EQ(output.front().label,"beep");
    EXPECT_EQ(output.front().ending_frame,2u);
    matcher.pushInto(source(1),{100},output);
    EXPECT_TRUE(output.empty());
}
