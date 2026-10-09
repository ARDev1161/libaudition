#include <audition/spatial/temporal_smoother.hpp>

#include <cstdint>

#include <gtest/gtest.h>

namespace {

audition::Timestamp ts(
    std::int64_t nanoseconds,
    std::uint32_t source_id = 0U) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, source_id}};
}

audition::SpatialTrack trackAt(
    std::uint64_t track_id,
    std::int64_t last_seen,
    std::uint32_t clock_source = 0U) {
    audition::SpatialTrack track{};
    track.track_id = audition::SpatialTrackId{track_id};
    track.first_seen = ts(0, clock_source);
    track.last_seen = ts(last_seen, clock_source);
    return track;
}

audition::RangeEstimate range(
    double mean,
    double variance,
    audition::RangeEstimate::Method method =
        audition::RangeEstimate::Method::LevelPrior,
    double confidence = 0.7) {
    return audition::RangeEstimate{
        {mean, variance},
        audition::Probability::from(confidence),
        method};
}

audition::Covariance3 diagonalCovariance(
    double x,
    double y,
    double z) {
    return audition::Covariance3{
        x, 0.0, 0.0,
        0.0, y, 0.0,
        0.0, 0.0, z};
}

audition::PositionEstimate position(
    double x,
    double y,
    double z,
    audition::Covariance3 covariance,
    double confidence = 0.8) {
    audition::PositionEstimate result{};
    result.mean_m = {x, y, z};
    result.covariance_m2 = covariance;
    result.confidence = audition::Probability::from(confidence);
    return result;
}

}  // namespace

TEST(TemporalSpatialTrackSmoother, FirstRangeObservationPassesThrough) {
    audition::TemporalSpatialTrackSmoother smoother;

    auto track = trackAt(1U, 0);
    track.range = range(10.0, 4.0);

    smoother.update(track);

    ASSERT_TRUE(track.range.has_value());
    EXPECT_DOUBLE_EQ(track.range->distance_m.mean, 10.0);
    EXPECT_DOUBLE_EQ(track.range->distance_m.variance, 4.0);
    EXPECT_EQ(
        track.range->method,
        audition::RangeEstimate::Method::LevelPrior);
    EXPECT_DOUBLE_EQ(track.range->confidence.value(), 0.7);
    EXPECT_EQ(smoother.trackedCount(), 1U);
}

TEST(TemporalSpatialTrackSmoother, RangeUsesExactMixtureMoments) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.25;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(10.0, 4.0);
    smoother.update(first);

    auto second = trackAt(1U, 1'000'000'000LL);
    second.range = range(14.0, 9.0);
    smoother.update(second);

    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.mean, 11.0);
    EXPECT_DOUBLE_EQ(second.range->distance_m.variance, 8.25);
    EXPECT_EQ(
        second.range->method,
        audition::RangeEstimate::Method::Fused);
    EXPECT_DOUBLE_EQ(second.range->confidence.value(), 0.0);
}

TEST(TemporalSpatialTrackSmoother, DisagreementIncreasesRangeUncertainty) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.5;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(10.0, 1.0);
    smoother.update(first);

    auto second = trackAt(1U, 1);
    second.range = range(20.0, 1.0);
    smoother.update(second);

    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.mean, 15.0);
    EXPECT_DOUBLE_EQ(second.range->distance_m.variance, 26.0);
}

TEST(TemporalSpatialTrackSmoother, IdenticalRangeVarianceDoesNotCollapse) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.5;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(5.0, 4.0);
    smoother.update(first);

    auto second = trackAt(1U, 1);
    second.range = range(5.0, 4.0);
    smoother.update(second);

    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.mean, 5.0);
    EXPECT_DOUBLE_EQ(second.range->distance_m.variance, 4.0);
}

TEST(TemporalSpatialTrackSmoother, AddsExplicitRangeProcessVariance) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.5;
    options.range_process_variance_m2_per_s = 2.0;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(10.0, 1.0);
    smoother.update(first);

    auto second = trackAt(1U, 2'000'000'000LL);
    second.range = range(10.0, 1.0);
    smoother.update(second);

    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.variance, 3.0);
}

TEST(TemporalSpatialTrackSmoother, PositionUsesFullMomentCovariance) {
    audition::TemporalSpatialSmootherOptions options{};
    options.position_measurement_weight = 0.5;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.position = position(
        0.0,
        0.0,
        0.0,
        diagonalCovariance(1.0, 1.0, 1.0));
    smoother.update(first);

    auto second = trackAt(1U, 1);
    second.position = position(
        2.0,
        0.0,
        0.0,
        diagonalCovariance(1.0, 1.0, 1.0));
    smoother.update(second);

    ASSERT_TRUE(second.position.has_value());
    EXPECT_DOUBLE_EQ(second.position->mean_m.x, 1.0);
    EXPECT_DOUBLE_EQ(second.position->mean_m.y, 0.0);
    EXPECT_DOUBLE_EQ(second.position->mean_m.z, 0.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[0], 2.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[4], 1.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[8], 1.0);
    EXPECT_DOUBLE_EQ(second.position->confidence.value(), 0.0);
}

TEST(TemporalSpatialTrackSmoother, PositionDisagreementCreatesCrossCovariance) {
    audition::TemporalSpatialSmootherOptions options{};
    options.position_measurement_weight = 0.5;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.position = position(
        0.0,
        0.0,
        0.0,
        diagonalCovariance(0.0, 0.0, 0.0));
    smoother.update(first);

    auto second = trackAt(1U, 1);
    second.position = position(
        2.0,
        2.0,
        0.0,
        diagonalCovariance(0.0, 0.0, 0.0));
    smoother.update(second);

    ASSERT_TRUE(second.position.has_value());
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[0], 1.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[1], 1.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[3], 1.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[4], 1.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[8], 0.0);
}

TEST(TemporalSpatialTrackSmoother, GapBeyondLimitReinitializesRange) {
    audition::TemporalSpatialSmootherOptions options{};
    options.max_gap = audition::Duration{1'000'000'000LL};
    options.range_measurement_weight = 0.1;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(2.0, 1.0);
    smoother.update(first);

    auto second = trackAt(1U, 2'000'000'000LL);
    second.range = range(
        8.0,
        3.0,
        audition::RangeEstimate::Method::LevelPrior,
        0.6);
    smoother.update(second);

    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.mean, 8.0);
    EXPECT_DOUBLE_EQ(second.range->distance_m.variance, 3.0);
    EXPECT_EQ(
        second.range->method,
        audition::RangeEstimate::Method::LevelPrior);
    EXPECT_DOUBLE_EQ(second.range->confidence.value(), 0.6);
}

TEST(TemporalSpatialTrackSmoother, MissingMetricDoesNotAdvanceMetricTimestamp) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.5;
    options.range_process_variance_m2_per_s = 4.0;
    options.max_gap = audition::Duration{10'000'000'000LL};
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(10.0, 1.0);
    smoother.update(first);

    auto missing = trackAt(1U, 500'000'000LL);
    smoother.update(missing);

    auto second = trackAt(1U, 750'000'000LL);
    second.range = range(10.0, 1.0);
    smoother.update(second);

    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.variance, 2.5);
}

TEST(TemporalSpatialTrackSmoother, MissingMetricStillAdvancesTrackOrdering) {
    audition::TemporalSpatialTrackSmoother smoother;

    auto first = trackAt(1U, 0);
    first.range = range(10.0, 1.0);
    smoother.update(first);

    auto missing = trackAt(1U, 500);
    smoother.update(missing);

    auto out_of_order = trackAt(1U, 499);
    out_of_order.range = range(10.0, 1.0);

    EXPECT_THROW(
        smoother.update(out_of_order),
        audition::Error);
}

TEST(TemporalSpatialTrackSmoother, RejectsClockChangeForSameTrack) {
    audition::TemporalSpatialTrackSmoother smoother;

    auto first = trackAt(1U, 0, 1U);
    first.range = range(10.0, 1.0);
    smoother.update(first);

    auto second = trackAt(1U, 1, 2U);
    second.range = range(10.0, 1.0);

    try {
        smoother.update(second);
        FAIL() << "Expected clock mismatch";
    } catch (const audition::Error& error) {
        EXPECT_EQ(
            error.code(),
            audition::ErrorCode::ClockDomainMismatch);
    }
}

TEST(TemporalSpatialTrackSmoother, RejectsInvalidPositionCovarianceTransactionally) {
    audition::TemporalSpatialTrackSmoother smoother;

    auto invalid = trackAt(1U, 0);
    invalid.position = position(
        1.0,
        2.0,
        3.0,
        audition::Covariance3{
            1.0, 2.0, 0.0,
            2.0, 1.0, 0.0,
            0.0, 0.0, 1.0});

    EXPECT_THROW(
        smoother.update(invalid),
        audition::Error);
    EXPECT_EQ(smoother.trackedCount(), 0U);
}

TEST(TemporalSpatialTrackSmoother, RejectsAsymmetricPositionCovariance) {
    audition::TemporalSpatialTrackSmoother smoother;

    auto invalid = trackAt(1U, 0);
    invalid.position = position(
        1.0,
        2.0,
        3.0,
        audition::Covariance3{
            1.0, 0.2, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0});

    EXPECT_THROW(
        smoother.update(invalid),
        audition::Error);
}

TEST(TemporalSpatialTrackSmoother, MeasurementWeightOnePreservesMeasurementProvenance) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 1.0;
    options.position_measurement_weight = 1.0;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(1.0, 1.0);
    first.position = position(
        1.0,
        1.0,
        1.0,
        diagonalCovariance(1.0, 1.0, 1.0));
    smoother.update(first);

    auto second = trackAt(1U, 1);
    second.range = range(
        7.0,
        2.0,
        audition::RangeEstimate::Method::BearingTriangulation,
        0.9);
    second.position = position(
        7.0,
        8.0,
        9.0,
        diagonalCovariance(2.0, 3.0, 4.0),
        0.95);
    smoother.update(second);

    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.mean, 7.0);
    EXPECT_EQ(
        second.range->method,
        audition::RangeEstimate::Method::BearingTriangulation);
    EXPECT_DOUBLE_EQ(second.range->confidence.value(), 0.9);

    ASSERT_TRUE(second.position.has_value());
    EXPECT_DOUBLE_EQ(second.position->mean_m.x, 7.0);
    EXPECT_DOUBLE_EQ(second.position->confidence.value(), 0.95);
}

TEST(TemporalSpatialTrackSmoother, EndTrackPreventsIdReuseFromInheritingState) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.1;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 100);
    first.range = range(1.0, 1.0);
    smoother.update(first);

    smoother.endTrack(first.track_id);
    EXPECT_EQ(smoother.trackedCount(), 0U);

    auto reused = trackAt(1U, 0);
    reused.range = range(9.0, 2.0);
    smoother.update(reused);

    ASSERT_TRUE(reused.range.has_value());
    EXPECT_DOUBLE_EQ(reused.range->distance_m.mean, 9.0);
    EXPECT_EQ(smoother.trackedCount(), 1U);
}

TEST(TemporalSpatialTrackSmoother, ResetClearsAllState) {
    audition::TemporalSpatialTrackSmoother smoother;

    auto first = trackAt(1U, 0);
    first.range = range(1.0, 1.0);
    smoother.update(first);

    auto second = trackAt(2U, 0);
    second.position = position(
        0.0,
        0.0,
        0.0,
        diagonalCovariance(1.0, 1.0, 1.0));
    smoother.update(second);

    EXPECT_EQ(smoother.trackedCount(), 2U);
    smoother.reset();
    EXPECT_EQ(smoother.trackedCount(), 0U);
}

TEST(TemporalSpatialTrackSmoother, EnforcesBoundedTrackState) {
    audition::TemporalSpatialSmootherOptions options{};
    options.max_tracks = 1U;
    audition::TemporalSpatialTrackSmoother smoother{options};

    auto first = trackAt(1U, 0);
    first.range = range(1.0, 1.0);
    smoother.update(first);

    auto second = trackAt(2U, 0);
    second.range = range(2.0, 1.0);
    EXPECT_THROW(
        smoother.update(second),
        audition::Error);

    smoother.endTrack(first.track_id);
    EXPECT_NO_THROW(smoother.update(second));
}

TEST(TemporalSpatialTrackSmoother, RejectsInvalidOptions) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 1.1;
    EXPECT_THROW(
        audition::TemporalSpatialTrackSmoother{options},
        audition::Error);

    options = {};
    options.position_process_variance_m2_per_s = -1.0;
    EXPECT_THROW(
        audition::TemporalSpatialTrackSmoother{options},
        audition::Error);

    options = {};
    options.max_gap = audition::Duration{-1};
    EXPECT_THROW(
        audition::TemporalSpatialTrackSmoother{options},
        audition::Error);

    options = {};
    options.max_tracks = 0U;
    EXPECT_THROW(
        audition::TemporalSpatialTrackSmoother{options},
        audition::Error);
}
