#include <audition/spatial/level_range_prior.hpp>
#include <audition/spatial/spl_calibration.hpp>

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

audition::SoundPressureCalibrationProfile profile() {
    audition::SoundPressureCalibrationProfile result{};
    result.profile_id = "respeaker-array-v2-path-1";
    result.signal_path_id = "frontend.route.0";
    result.reference_level_db_spl = {94.0, 0.25};
    result.measured_level_dbfs = {-26.0, 0.36};
    result.reference_frequency_hz = 1000.0;
    result.weighting = audition::SoundLevelWeighting::Z;
    result.transfer_variance_db2 = 0.39;
    return result;
}

audition::DbfsLevelObservation observation(
    double level_dbfs,
    double variance_db2 = 0.0) {
    audition::DbfsLevelObservation result{};
    result.timestamp = ts(123);
    result.level_dbfs = level_dbfs;
    result.variance_db2 = variance_db2;
    result.weighting = audition::SoundLevelWeighting::Z;
    result.sensor_pose.position = {1.0, 2.0, 3.0};
    result.signal_path_id = "frontend.route.0";
    return result;
}

}  // namespace

TEST(SplCalibration, DerivesAuditableOffsetFromReference) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    EXPECT_DOUBLE_EQ(calibrator.offsetDb(), 120.0);
    EXPECT_DOUBLE_EQ(calibrator.calibrationVarianceDb2(), 1.0);
    EXPECT_EQ(calibrator.profile().profile_id, "respeaker-array-v2-path-1");
}

TEST(SplCalibration, ConvertsDbfsToDbSplAndPropagatesVariance) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    const auto result = calibrator.calibrate(observation(-40.0, 0.5));
    ASSERT_TRUE(result.has_value());

    EXPECT_DOUBLE_EQ(result->level_db_spl.mean, 80.0);
    EXPECT_DOUBLE_EQ(result->level_db_spl.variance, 1.5);
    EXPECT_EQ(result->timestamp.nanoseconds(), 123);
    EXPECT_DOUBLE_EQ(result->sensor_pose.position.x, 1.0);
    EXPECT_DOUBLE_EQ(result->sensor_pose.position.y, 2.0);
    EXPECT_DOUBLE_EQ(result->sensor_pose.position.z, 3.0);
    EXPECT_EQ(result->weighting, audition::SoundLevelWeighting::Z);
}

TEST(SplCalibration, ExactZeroRmsProducesNoFiniteSplObservation) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    auto silent = observation(
        -std::numeric_limits<double>::infinity());

    EXPECT_FALSE(calibrator.calibrate(silent).has_value());
}

TEST(SplCalibration, RejectsWrongSignalPath) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    auto measured = observation(-40.0);
    measured.signal_path_id = "frontend.route.1";

    EXPECT_THROW(
        static_cast<void>(calibrator.calibrate(measured)),
        audition::Error);
}

TEST(SplCalibration, RejectsWeightingMismatch) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    auto measured = observation(-40.0);
    measured.weighting = audition::SoundLevelWeighting::A;

    EXPECT_THROW(
        static_cast<void>(calibrator.calibrate(measured)),
        audition::Error);
}

TEST(SplCalibration, RejectsInvalidObservationVariance) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    auto measured = observation(-40.0, -1.0);

    EXPECT_THROW(
        static_cast<void>(calibrator.calibrate(measured)),
        audition::Error);
}

TEST(SplCalibration, RejectsNaNAndPositiveInfinityLevels) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    auto measured = observation(
        std::numeric_limits<double>::quiet_NaN());
    EXPECT_THROW(
        static_cast<void>(calibrator.calibrate(measured)),
        audition::Error);

    measured = observation(
        std::numeric_limits<double>::infinity());
    EXPECT_THROW(
        static_cast<void>(calibrator.calibrate(measured)),
        audition::Error);
}

TEST(SplCalibration, RejectsInvalidPose) {
    const audition::SoundPressureLevelCalibrator calibrator{profile()};

    auto measured = observation(-40.0);
    measured.sensor_pose.orientation = {0.0, 0.0, 0.0, 0.0};

    EXPECT_THROW(
        static_cast<void>(calibrator.calibrate(measured)),
        audition::Error);
}

TEST(SplCalibration, RejectsMalformedProfiles) {
    auto invalid = profile();
    invalid.profile_id.clear();
    EXPECT_THROW(
        audition::SoundPressureLevelCalibrator{invalid},
        audition::Error);

    invalid = profile();
    invalid.signal_path_id.clear();
    EXPECT_THROW(
        audition::SoundPressureLevelCalibrator{invalid},
        audition::Error);

    invalid = profile();
    invalid.reference_frequency_hz = 0.0;
    EXPECT_THROW(
        audition::SoundPressureLevelCalibrator{invalid},
        audition::Error);

    invalid = profile();
    invalid.reference_level_db_spl.variance = -1.0;
    EXPECT_THROW(
        audition::SoundPressureLevelCalibrator{invalid},
        audition::Error);

    invalid = profile();
    invalid.measured_level_dbfs.mean =
        std::numeric_limits<double>::infinity();
    EXPECT_THROW(
        audition::SoundPressureLevelCalibrator{invalid},
        audition::Error);

    invalid = profile();
    invalid.transfer_variance_db2 = -1.0;
    EXPECT_THROW(
        audition::SoundPressureLevelCalibrator{invalid},
        audition::Error);
}

TEST(SplCalibration, FeedsCalibratedObservationIntoRangePrior) {
    auto calibration = profile();
    calibration.reference_level_db_spl = {94.0, 0.0};
    calibration.measured_level_dbfs = {-26.0, 0.0};
    calibration.transfer_variance_db2 = 0.0;

    const audition::SoundPressureLevelCalibrator calibrator{
        calibration};
    const auto level = calibrator.calibrate(observation(-60.0));
    ASSERT_TRUE(level.has_value());
    EXPECT_DOUBLE_EQ(level->level_db_spl.mean, 60.0);

    audition::SourceLevelPrior priors[] = {
        {
            "alarm",
            {80.0, 0.0},
            1.0,
            1.0,
            audition::SoundLevelWeighting::Z,
        },
    };

    audition::RangeEstimationInput input{};
    input.sound_level = *level;
    input.source_level_priors = priors;

    const audition::SoundLevelRangePriorEstimator estimator;
    const auto range = estimator.estimate(input);
    ASSERT_TRUE(range.has_value());
    EXPECT_NEAR(range->distance_m.mean, 10.0, 1.0e-12);
    EXPECT_EQ(
        range->method,
        audition::RangeEstimate::Method::LevelPrior);
}
