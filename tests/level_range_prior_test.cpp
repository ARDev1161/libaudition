#include <audition/spatial/level_range_prior.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

audition::Timestamp ts(std::int64_t nanoseconds) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, 0U}};
}

audition::SoundLevelObservation level(
    double mean_db_spl,
    double variance_db2 = 0.0,
    audition::SoundLevelWeighting weighting =
        audition::SoundLevelWeighting::Z) {
    audition::SoundLevelObservation result{};
    result.timestamp = ts(123);
    result.level_db_spl = {mean_db_spl, variance_db2};
    result.weighting = weighting;
    result.sensor_pose.position = {1.0, 2.0, 3.0};
    return result;
}

audition::SourceLevelPrior prior(
    const char* source_type,
    double mean_db_spl,
    double variance_db2 = 0.0,
    double reference_distance_m = 1.0,
    double weight = 1.0,
    audition::SoundLevelWeighting weighting =
        audition::SoundLevelWeighting::Z) {
    audition::SourceLevelPrior result{};
    result.source_type = source_type;
    result.level_db_spl_at_reference = {
        mean_db_spl,
        variance_db2};
    result.reference_distance_m = reference_distance_m;
    result.weight = weight;
    result.weighting = weighting;
    return result;
}

}  // namespace

TEST(SoundLevelRangePrior, FreeFieldTwentyDbMeansTenTimesDistance) {
    audition::SoundLevelRangePriorEstimator estimator;

    const auto measured = level(60.0);
    audition::SourceLevelPrior priors[] = {
        prior("alarm", 80.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    const auto estimate = estimator.estimate(input);
    ASSERT_TRUE(estimate.has_value());
    EXPECT_NEAR(estimate->distance_m.mean, 10.0, 1.0e-12);
    EXPECT_NEAR(estimate->distance_m.variance, 0.0, 1.0e-12);
    EXPECT_EQ(
        estimate->method,
        audition::RangeEstimate::Method::LevelPrior);
    EXPECT_DOUBLE_EQ(estimate->confidence.value(), 0.0);
}

TEST(SoundLevelRangePrior, SupportsExplicitPathLossExponent) {
    audition::SoundLevelRangePriorOptions options{};
    options.path_loss_exponent = 4.0;
    audition::SoundLevelRangePriorEstimator estimator{options};

    const auto measured = level(60.0);
    audition::SourceLevelPrior priors[] = {
        prior("machine", 80.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    const auto estimate = estimator.estimate(input);
    ASSERT_TRUE(estimate.has_value());
    EXPECT_NEAR(
        estimate->distance_m.mean,
        std::sqrt(10.0),
        1.0e-12);
}

TEST(SoundLevelRangePrior, PropagatesDbUncertaintyAsLogNormalMoments) {
    audition::SoundLevelRangePriorOptions options{};
    options.propagation_variance_db2 = 9.0;
    audition::SoundLevelRangePriorEstimator estimator{options};

    const auto measured = level(60.0, 1.0);
    audition::SourceLevelPrior priors[] = {
        prior("alarm", 80.0, 4.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    const auto estimate = estimator.estimate(input);
    ASSERT_TRUE(estimate.has_value());

    EXPECT_NEAR(
        estimate->distance_m.mean,
        10.972238499332915,
        1.0e-12);
    EXPECT_NEAR(
        estimate->distance_m.variance,
        24.547545898696626,
        1.0e-11);
}

TEST(SoundLevelRangePrior, CombinesSourceTypesByExactMixtureMoments) {
    audition::SoundLevelRangePriorEstimator estimator;

    const auto measured = level(60.0);
    audition::SourceLevelPrior priors[] = {
        prior("loud_alarm", 80.0, 0.0, 1.0, 1.0),
        prior("quiet_alarm", 60.0, 0.0, 1.0, 3.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    const auto estimate = estimator.estimate(input);
    ASSERT_TRUE(estimate.has_value());

    EXPECT_NEAR(estimate->distance_m.mean, 3.25, 1.0e-12);
    EXPECT_NEAR(
        estimate->distance_m.variance,
        15.1875,
        1.0e-12);
}

TEST(SoundLevelRangePrior, NormalizesRelativeWeights) {
    audition::SoundLevelRangePriorEstimator estimator;

    const auto measured = level(60.0);
    audition::SourceLevelPrior priors[] = {
        prior("a", 80.0, 0.0, 1.0, 2.0),
        prior("b", 60.0, 0.0, 1.0, 2.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    const auto estimate = estimator.estimate(input);
    ASSERT_TRUE(estimate.has_value());
    EXPECT_NEAR(estimate->distance_m.mean, 5.5, 1.0e-12);
    EXPECT_NEAR(estimate->distance_m.variance, 20.25, 1.0e-12);
}

TEST(SoundLevelRangePrior, ZeroWeightHypothesesDoNotContribute) {
    audition::SoundLevelRangePriorEstimator estimator;

    const auto measured = level(60.0);
    audition::SourceLevelPrior priors[] = {
        prior("ignored", 120.0, 0.0, 1.0, 0.0),
        prior("used", 60.0, 0.0, 1.0, 1.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    const auto estimate = estimator.estimate(input);
    ASSERT_TRUE(estimate.has_value());
    EXPECT_NEAR(estimate->distance_m.mean, 1.0, 1.0e-12);
}

TEST(SoundLevelRangePrior, AllZeroWeightsYieldNoEstimate) {
    audition::SoundLevelRangePriorEstimator estimator;

    const auto measured = level(60.0);
    audition::SourceLevelPrior priors[] = {
        prior("a", 80.0, 0.0, 1.0, 0.0),
        prior("b", 60.0, 0.0, 1.0, 0.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    EXPECT_FALSE(estimator.estimate(input).has_value());
}

TEST(SoundLevelRangePrior, MissingLevelOrPriorsYieldNoEstimate) {
    audition::SoundLevelRangePriorEstimator estimator;

    audition::RangeEstimationInput empty{};
    EXPECT_FALSE(estimator.estimate(empty).has_value());

    auto measured = level(60.0);
    empty.sound_level = measured;
    EXPECT_FALSE(estimator.estimate(empty).has_value());
}

TEST(SoundLevelRangePrior, RejectsWeightingMismatch) {
    audition::SoundLevelRangePriorEstimator estimator;

    const auto measured = level(
        60.0,
        0.0,
        audition::SoundLevelWeighting::A);
    audition::SourceLevelPrior priors[] = {
        prior(
            "alarm",
            80.0,
            0.0,
            1.0,
            1.0,
            audition::SoundLevelWeighting::Z),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    EXPECT_THROW(
        static_cast<void>(estimator.estimate(input)),
        audition::Error);
}

TEST(SoundLevelRangePrior, RejectsInvalidPriorAndMeasurement) {
    audition::SoundLevelRangePriorEstimator estimator;

    auto measured = level(60.0, -1.0);
    audition::SourceLevelPrior priors[] = {
        prior("alarm", 80.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;
    EXPECT_THROW(
        static_cast<void>(estimator.estimate(input)),
        audition::Error);

    measured = level(60.0);
    priors[0].source_type.clear();
    input.sound_level = measured;
    EXPECT_THROW(
        static_cast<void>(estimator.estimate(input)),
        audition::Error);

    priors[0] = prior("alarm", 80.0);
    priors[0].reference_distance_m = 0.0;
    EXPECT_THROW(
        static_cast<void>(estimator.estimate(input)),
        audition::Error);

    priors[0] = prior("alarm", 80.0);
    priors[0].weight = -1.0;
    EXPECT_THROW(
        static_cast<void>(estimator.estimate(input)),
        audition::Error);
}

TEST(SoundLevelRangePrior, RejectsInvalidOptions) {
    audition::SoundLevelRangePriorOptions options{};
    options.path_loss_exponent = 0.0;
    EXPECT_THROW(
        audition::SoundLevelRangePriorEstimator{options},
        audition::Error);

    options.path_loss_exponent = 2.0;
    options.propagation_variance_db2 = -1.0;
    EXPECT_THROW(
        audition::SoundLevelRangePriorEstimator{options},
        audition::Error);
}

TEST(SoundLevelRangePrior, BuildsPoseAnchoredScalarObservation) {
    audition::SoundLevelRangePriorEstimator estimator;

    const auto measured = level(60.0);
    audition::SourceLevelPrior priors[] = {
        prior("alarm", 80.0),
    };

    audition::RangeEstimationInput input{};
    input.sound_level = measured;
    input.source_level_priors = priors;

    const auto observation = estimator.estimateObservation(input);
    ASSERT_TRUE(observation.has_value());

    EXPECT_EQ(
        observation->timestamp.nanoseconds(),
        measured.timestamp.nanoseconds());
    EXPECT_DOUBLE_EQ(
        observation->sensor_pose.position.x,
        measured.sensor_pose.position.x);
    EXPECT_NEAR(observation->distance_m.mean, 10.0, 1.0e-12);
    EXPECT_EQ(
        observation->method,
        audition::RangeEstimate::Method::LevelPrior);
    EXPECT_DOUBLE_EQ(observation->confidence.value(), 0.0);
}

TEST(SoundLevelRangePrior, BackendInfoIdentifiesEstimator) {
    const audition::SoundLevelRangePriorEstimator estimator;
    const auto info = estimator.backendInfo();
    EXPECT_EQ(info.name, "sound-level-prior");
    EXPECT_EQ(info.implementation_version, "1");
}


TEST(SoundLevelRangePrior, RejectsNonfiniteOptionsAndSpatialObservation) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double bad : {nan, inf, -inf}) {
        audition::SoundLevelRangePriorOptions options{};
        options.path_loss_exponent = bad;
        EXPECT_THROW(static_cast<void>(audition::SoundLevelRangePriorEstimator{options}), audition::Error);
        options = {};
        options.propagation_variance_db2 = bad;
        EXPECT_THROW(static_cast<void>(audition::SoundLevelRangePriorEstimator{options}), audition::Error);
    }

    const audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {prior("machine", 90.0)};
    audition::RangeEstimationInput input{};
    input.source_level_priors = priors;
    for (const double bad : {nan, inf, -inf}) {
        auto measured = level(60.0);
        measured.sensor_pose.position.x = bad;
        input.sound_level = measured;
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
        measured = level(60.0);
        measured.sensor_pose.position.y = bad;
        input.sound_level = measured;
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
        measured = level(60.0);
        measured.sensor_pose.position.z = bad;
        input.sound_level = measured;
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
    }
}

TEST(SoundLevelRangePrior, RejectsMalformedHypothesesAndOverflow) {
    const audition::SoundLevelRangePriorEstimator estimator;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    audition::SourceLevelPrior priors[] = {prior("alarm", 80.0)};
    audition::RangeEstimationInput input{};
    input.sound_level = level(60.0);
    input.source_level_priors = priors;

    for (const double bad : {nan, inf, -inf}) {
        priors[0] = prior("alarm", 80.0);
        priors[0].reference_distance_m = bad;
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
        priors[0] = prior("alarm", 80.0);
        priors[0].weight = bad;
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
    }
    priors[0] = prior("alarm", 80.0);
    priors[0].level_db_spl_at_reference.variance = -1.0;
    EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
    priors[0] = prior("alarm", 20000.0);
    EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
    priors[0] = prior("alarm", 80.0);
    EXPECT_TRUE(estimator.estimate(input).has_value());
}

TEST(SoundLevelRangePrior, MissingHypothesesYieldNoScalarObservation) {
    const audition::SoundLevelRangePriorEstimator estimator;
    audition::RangeEstimationInput input{};
    EXPECT_FALSE(estimator.estimateObservation(input).has_value());
    input.sound_level = level(60.0);
    EXPECT_FALSE(estimator.estimateObservation(input).has_value());
}


TEST(SoundLevelRangePrior, PreservesExplicitModelOptions) {
    audition::SoundLevelRangePriorOptions options{};
    options.path_loss_exponent = 3.5;
    options.propagation_variance_db2 = 1.25;
    audition::SoundLevelRangePriorEstimator estimator{options};
    EXPECT_DOUBLE_EQ(estimator.options().path_loss_exponent, 3.5);
    EXPECT_DOUBLE_EQ(estimator.options().propagation_variance_db2, 1.25);
}

TEST(SoundLevelRangePrior, ReferenceDistanceScalesPredictedDistance) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {
        prior("machine", 80.0, 0.0, 2.5)
    };
    audition::RangeEstimationInput input{};
    input.sound_level = level(60.0);
    input.source_level_priors = priors;
    const auto result = estimator.estimate(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->distance_m.mean, 25.0, 1.0e-10);
    EXPECT_NEAR(result->distance_m.variance, 0.0, 1.0e-9);
}

TEST(SoundLevelRangePrior, HigherMeasuredLevelYieldsShorterDistance) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {
        prior("machine", 80.0)
    };
    audition::RangeEstimationInput input{};
    input.sound_level = level(100.0);
    input.source_level_priors = priors;
    const auto close = estimator.estimate(input);
    ASSERT_TRUE(close.has_value());
    EXPECT_NEAR(close->distance_m.mean, 0.1, 1.0e-12);
    EXPECT_GE(close->distance_m.variance, 0.0);
}

TEST(SoundLevelRangePrior, EqualAWeightedLevelsYieldReferenceDistance) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {
        prior("speech", 70.0, 0.0, 3.0, 1.0,
              audition::SoundLevelWeighting::A)
    };
    audition::RangeEstimationInput input{};
    input.sound_level = level(70.0, 0.0, audition::SoundLevelWeighting::A);
    input.source_level_priors = priors;
    const auto result = estimator.estimate(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->distance_m.mean, 3.0, 1.0e-12);
    EXPECT_DOUBLE_EQ(result->confidence.value(), 0.0);
}

TEST(SoundLevelRangePrior, RejectsNonfiniteMeasuredDbMoments) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {prior("machine", 80.0)};
    audition::RangeEstimationInput input{};
    input.source_level_priors = priors;
    for (const double bad : {
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity()}) {
        input.sound_level = level(bad);
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)),
                     audition::Error);
        input.sound_level = level(60.0, bad);
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)),
                     audition::Error);
    }
}

TEST(SoundLevelRangePrior, RejectsNonfiniteSourceLevelMoments) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {prior("machine", 80.0)};
    audition::RangeEstimationInput input{};
    input.sound_level = level(60.0);
    input.source_level_priors = priors;
    for (const double bad : {
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity()}) {
        priors[0] = prior("machine", bad);
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)),
                     audition::Error);
        priors[0] = prior("machine", 80.0, bad);
        EXPECT_THROW(static_cast<void>(estimator.estimate(input)),
                     audition::Error);
    }
}

TEST(SoundLevelRangePrior, ValidatesZeroWeightPriorsInsteadOfIgnoringMalformedData) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {
        prior("valid", 80.0),
        prior("invalid", 80.0, 0.0, 0.0, 0.0)
    };
    audition::RangeEstimationInput input{};
    input.sound_level = level(60.0);
    input.source_level_priors = priors;
    EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
    priors[1] = prior("ignored", 100.0, 0.0, 1.0, 0.0);
    const auto estimate = estimator.estimate(input);
    ASSERT_TRUE(estimate.has_value());
    EXPECT_NEAR(estimate->distance_m.mean, 10.0, 1.0e-12);
}

TEST(SoundLevelRangePrior, RejectsOverflowOfTotalMixtureWeight) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {
        prior("a", 60.0, 0.0, 1.0, 1.0e308),
        prior("b", 60.0, 0.0, 1.0, 1.0e308)
    };
    audition::RangeEstimationInput input{};
    input.sound_level = level(60.0);
    input.source_level_priors = priors;
    EXPECT_THROW(static_cast<void>(estimator.estimate(input)), audition::Error);
}

TEST(SoundLevelRangePrior, NormalizedMixtureInvariantUnderWeightScaling) {
    audition::SoundLevelRangePriorEstimator estimator;
    audition::SourceLevelPrior priors[] = {
        prior("a", 80.0, 0.0, 1.0, 1.0),
        prior("b", 60.0, 0.0, 1.0, 3.0)
    };
    audition::RangeEstimationInput input{};
    input.sound_level = level(60.0);
    input.source_level_priors = priors;
    const auto original = estimator.estimate(input);
    ASSERT_TRUE(original.has_value());
    priors[0].weight = 10.0;
    priors[1].weight = 30.0;
    const auto scaled = estimator.estimate(input);
    ASSERT_TRUE(scaled.has_value());
    EXPECT_NEAR(original->distance_m.mean, scaled->distance_m.mean, 1.0e-12);
    EXPECT_NEAR(original->distance_m.variance, scaled->distance_m.variance,
                1.0e-10);
}

TEST(SoundLevelRangePrior, ScalarObservationPreservesUncertaintyAndSensorPose) {
    audition::SoundLevelRangePriorOptions options{};
    options.propagation_variance_db2 = 4.0;
    audition::SoundLevelRangePriorEstimator estimator{options};
    audition::SourceLevelPrior priors[] = {prior("machine", 80.0, 2.0)};
    audition::RangeEstimationInput input{};
    auto measured = level(60.0, 1.0);
    measured.timestamp = ts(9876);
    measured.sensor_pose.position = {-1.0, -2.0, 3.0};
    input.sound_level = measured;
    input.source_level_priors = priors;
    const auto scalar = estimator.estimateObservation(input);
    const auto direct = estimator.estimate(input);
    ASSERT_TRUE(scalar.has_value());
    ASSERT_TRUE(direct.has_value());
    EXPECT_EQ(scalar->timestamp.nanoseconds(), 9876);
    EXPECT_DOUBLE_EQ(scalar->sensor_pose.position.x, -1.0);
    EXPECT_DOUBLE_EQ(scalar->sensor_pose.position.y, -2.0);
    EXPECT_DOUBLE_EQ(scalar->sensor_pose.position.z, 3.0);
    EXPECT_DOUBLE_EQ(scalar->distance_m.mean, direct->distance_m.mean);
    EXPECT_DOUBLE_EQ(scalar->distance_m.variance, direct->distance_m.variance);
    EXPECT_GT(scalar->distance_m.variance, 0.0);
    EXPECT_EQ(scalar->method, audition::RangeEstimate::Method::LevelPrior);
}
