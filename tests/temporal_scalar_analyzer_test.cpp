#include <audition/temporal/scalar_analyzer.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
audition::Timestamp timeAt(std::int64_t n, unsigned clock = 1) {
    return {n, {audition::ClockDomain::Simulation, clock}};
}
audition::temporal::ScalarObservation obs(unsigned id, std::int64_t n,
                                           double v, std::string feature = "alarm") {
    return {audition::AcousticSourceId{id}, std::move(feature), timeAt(n), v};
}
}  // namespace

TEST(TemporalScalarAnalyzer, EmaAndPositiveNegativeCusum) {
    audition::temporal::TemporalScalarOptions opts{};
    opts.alpha = 0.5;
    opts.drift = 0.0;
    opts.threshold = 0.75;
    audition::temporal::TemporalScalarAnalyzer analyzer(opts);
    EXPECT_DOUBLE_EQ(analyzer.observe(obs(1, 1, 0)).smoothed, 0.0);
    auto rise = analyzer.observe(obs(1, 2, 1));
    EXPECT_TRUE(rise.positive_change);
    EXPECT_FALSE(rise.negative_change);
    EXPECT_DOUBLE_EQ(rise.smoothed, 0.5);
    auto fall = analyzer.observe(obs(1, 3, -1));
    EXPECT_FALSE(fall.positive_change);
    EXPECT_TRUE(fall.negative_change);
    EXPECT_DOUBLE_EQ(fall.smoothed, -0.25);
}

TEST(TemporalScalarAnalyzer, TracksSourcesAndFeaturesSeparately) {
    audition::temporal::TemporalScalarAnalyzer analyzer;
    (void)analyzer.observe(obs(1, 1, 1, "speech"));
    (void)analyzer.observe(obs(2, 1, 3, "speech"));
    (void)analyzer.observe(obs(1, 1, 5, "alarm"));
    EXPECT_EQ(analyzer.sourceCount(), 2u);
    auto result = analyzer.observe(obs(1, 2, 1, "speech"));
    EXPECT_DOUBLE_EQ(result.smoothed, 1);
    analyzer.forget(audition::AcousticSourceId{1});
    EXPECT_EQ(analyzer.sourceCount(), 1u);
    EXPECT_DOUBLE_EQ(analyzer.observe(obs(1, 1, 9, "speech")).smoothed, 9);
    analyzer.reset();
    EXPECT_EQ(analyzer.sourceCount(), 0u);
}

TEST(TemporalScalarAnalyzer, RejectsClocksAndTimeRegression) {
    audition::temporal::TemporalScalarAnalyzer analyzer;
    (void)analyzer.observe(obs(1, 10, 1));
    EXPECT_THROW(static_cast<void>(analyzer.observe(obs(1, 10, 2))), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(analyzer.observe(obs(1, 9, 2))), std::invalid_argument);
    auto wrong = obs(1, 11, 2);
    wrong.time = timeAt(11, 2);
    EXPECT_THROW(static_cast<void>(analyzer.observe(wrong)), std::invalid_argument);
    EXPECT_DOUBLE_EQ(analyzer.observe(obs(1, 11, 1)).smoothed, 1);
}

TEST(TemporalScalarAnalyzer, BadInputAndCapacityDoNotConsumeState) {
    audition::temporal::TemporalScalarOptions options{};
    options.max_sources = 1;
    options.max_features_per_source = 1;
    audition::temporal::TemporalScalarAnalyzer analyzer(options);
    (void)analyzer.observe(obs(1, 1, 0));
    EXPECT_THROW(static_cast<void>(analyzer.observe(obs(2, 2, 0))), std::length_error);
    EXPECT_THROW(static_cast<void>(analyzer.observe(obs(1, 2, 0, "other"))), std::length_error);
    EXPECT_THROW(static_cast<void>(analyzer.observe(obs(1, 2, std::numeric_limits<double>::quiet_NaN()))),
                 std::invalid_argument);
    EXPECT_EQ(analyzer.sourceCount(), 1u);
    EXPECT_NO_THROW(static_cast<void>(analyzer.observe(obs(1, 2, 0))));
}

TEST(TemporalScalarAnalyzer, InvalidConfigurationRejected) {
    audition::temporal::TemporalScalarOptions o{};
    o.alpha = 0;
    EXPECT_THROW(audition::temporal::TemporalScalarAnalyzer{o}, std::invalid_argument);
    o.alpha = 0.5;
    o.threshold = -1;
    EXPECT_THROW(audition::temporal::TemporalScalarAnalyzer{o}, std::invalid_argument);
}
