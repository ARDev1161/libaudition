#include <audition/spatial/temporal_smoother.hpp>

#include <cstdint>
#include <limits>

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


TEST(TemporalSpatialTrackSmoother, EmptyUnknownTrackDoesNotConsumeCapacity) {
    audition::TemporalSpatialSmootherOptions options{};
    options.max_tracks = 1U;
    audition::TemporalSpatialTrackSmoother smoother{options};
    auto unknown = trackAt(1U, 0);
    smoother.update(unknown);
    EXPECT_EQ(smoother.trackedCount(), 0U);

    auto known = trackAt(2U, 1);
    known.range = range(3.0, 0.5);
    smoother.update(known);
    EXPECT_EQ(smoother.trackedCount(), 1U);
    auto still_empty = trackAt(3U, 2);
    EXPECT_NO_THROW(smoother.update(still_empty));
    EXPECT_EQ(smoother.trackedCount(), 1U);
}

TEST(TemporalSpatialTrackSmoother, ExactGapBoundaryBlendsAndLaterGapResets) {
    audition::TemporalSpatialSmootherOptions options{};
    options.max_gap = audition::Duration{10};
    options.range_measurement_weight = 0.5;
    audition::TemporalSpatialTrackSmoother smoother{options};
    auto first = trackAt(1U, 0);
    first.range = range(2.0, 1.0);
    smoother.update(first);

    auto boundary = trackAt(1U, 10);
    boundary.range = range(8.0, 1.0);
    smoother.update(boundary);
    ASSERT_TRUE(boundary.range.has_value());
    EXPECT_DOUBLE_EQ(boundary.range->distance_m.mean, 5.0);
    EXPECT_EQ(boundary.range->method, audition::RangeEstimate::Method::Fused);

    auto gap = trackAt(1U, 21);
    gap.range = range(9.0, 2.0);
    smoother.update(gap);
    ASSERT_TRUE(gap.range.has_value());
    EXPECT_DOUBLE_EQ(gap.range->distance_m.mean, 9.0);
    EXPECT_EQ(gap.range->method, audition::RangeEstimate::Method::LevelPrior);
}

TEST(TemporalSpatialTrackSmoother, ZeroRangeMeasurementWeightRetainsPredictedState) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.0;
    options.range_process_variance_m2_per_s = 2.0;
    audition::TemporalSpatialTrackSmoother smoother{options};
    auto first = trackAt(1U, 0);
    first.range = range(4.0, 1.0);
    smoother.update(first);
    auto second = trackAt(1U, 1'000'000'000LL);
    second.range = range(100.0, 4.0);
    smoother.update(second);
    ASSERT_TRUE(second.range.has_value());
    EXPECT_DOUBLE_EQ(second.range->distance_m.mean, 4.0);
    EXPECT_DOUBLE_EQ(second.range->distance_m.variance, 3.0);
    EXPECT_EQ(second.range->method, audition::RangeEstimate::Method::Fused);
}

TEST(TemporalSpatialTrackSmoother, PositionProcessNoiseIncreasesDiagonalVariance) {
    audition::TemporalSpatialSmootherOptions options{};
    options.position_measurement_weight = 0.5;
    options.position_process_variance_m2_per_s = 2.0;
    audition::TemporalSpatialTrackSmoother smoother{options};
    auto first = trackAt(2U, 0);
    first.position = position(0.0, 0.0, 0.0, diagonalCovariance(1.0, 1.0, 1.0));
    smoother.update(first);
    auto second = trackAt(2U, 1'000'000'000LL);
    second.position = position(0.0, 0.0, 0.0, diagonalCovariance(1.0, 1.0, 1.0));
    smoother.update(second);
    ASSERT_TRUE(second.position.has_value());
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[0], 2.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[4], 2.0);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[8], 2.0);
}

TEST(TemporalSpatialTrackSmoother, InvalidTrackLifecycleNeverAllocatesState) {
    audition::TemporalSpatialTrackSmoother smoother;
    auto missing_id = trackAt(0U, 0);
    missing_id.range = range(1.0, 1.0);
    EXPECT_THROW(smoother.update(missing_id), audition::Error);

    auto conflicting_clock = trackAt(1U, 0);
    conflicting_clock.first_seen = ts(0, 1U);
    conflicting_clock.range = range(1.0, 1.0);
    EXPECT_THROW(smoother.update(conflicting_clock), audition::Error);

    auto reversed = trackAt(2U, -1);
    reversed.range = range(1.0, 1.0);
    EXPECT_THROW(smoother.update(reversed), audition::Error);
    EXPECT_EQ(smoother.trackedCount(), 0U);
}

TEST(TemporalSpatialTrackSmoother, NonfiniteAndNegativeMeasurementsAreRejected) {
    audition::TemporalSpatialTrackSmoother smoother;
    auto bad = trackAt(1U, 0);
    bad.range = range(-1.0, 1.0);
    EXPECT_THROW(smoother.update(bad), audition::Error);
    bad.range = range(1.0, -1.0);
    EXPECT_THROW(smoother.update(bad), audition::Error);
    bad.range.reset();

    bad.position = position(
        std::numeric_limits<double>::infinity(), 0.0, 0.0,
        diagonalCovariance(1.0, 1.0, 1.0));
    EXPECT_THROW(smoother.update(bad), audition::Error);
    bad.position = position(0.0, 0.0, 0.0,
        diagonalCovariance(1.0, std::numeric_limits<double>::quiet_NaN(), 1.0));
    EXPECT_THROW(smoother.update(bad), audition::Error);
    EXPECT_EQ(smoother.trackedCount(), 0U);
}

TEST(TemporalSpatialTrackSmoother, RejectsNegativeMinorsAndNegativeDeterminant) {
    audition::TemporalSpatialTrackSmoother smoother;
    auto bad = trackAt(3U, 0);
    bad.position = position(0.0, 0.0, 0.0,
        diagonalCovariance(-1.0, 1.0, 1.0));
    EXPECT_THROW(smoother.update(bad), audition::Error);

    bad.position = position(0.0, 0.0, 0.0,
        audition::Covariance3{1.0, 2.0, 0.0,
                             2.0, 1.0, 0.0,
                             0.0, 0.0, 1.0});
    EXPECT_THROW(smoother.update(bad), audition::Error);

    // All pairwise principal minors are non-negative, but the
    // full determinant is negative: this matrix is not PSD.
    bad.position = position(0.0, 0.0, 0.0,
        audition::Covariance3{1.0, 0.9, 0.9,
                             0.9, 1.0, 0.0,
                             0.9, 0.0, 1.0});
    EXPECT_THROW(smoother.update(bad), audition::Error);
    EXPECT_EQ(smoother.trackedCount(), 0U);
}

TEST(TemporalSpatialTrackSmoother, InvalidPositionUpdatePreservesRangeState) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_measurement_weight = 0.5;
    audition::TemporalSpatialTrackSmoother smoother{options};
    auto first = trackAt(5U, 0);
    first.range = range(2.0, 1.0);
    smoother.update(first);

    auto invalid = trackAt(5U, 1);
    invalid.range = range(100.0, 1.0);
    invalid.position = position(0.0, 0.0, 0.0,
        audition::Covariance3{1.0, 0.4, 0.0,
                             0.0, 1.0, 0.0,
                             0.0, 0.0, 1.0});
    EXPECT_THROW(smoother.update(invalid), audition::Error);
    EXPECT_EQ(smoother.trackedCount(), 1U);

    auto recovered = trackAt(5U, 2);
    recovered.range = range(4.0, 1.0);
    smoother.update(recovered);
    ASSERT_TRUE(recovered.range.has_value());
    EXPECT_DOUBLE_EQ(recovered.range->distance_m.mean, 3.0);
}

TEST(TemporalSpatialTrackSmoother, RangeProcessVarianceOverflowIsTransactional) {
    audition::TemporalSpatialSmootherOptions options{};
    options.range_process_variance_m2_per_s =
        std::numeric_limits<double>::max();
    audition::TemporalSpatialTrackSmoother smoother{options};
    auto first = trackAt(6U, 0);
    first.range = range(1.0, 1.0);
    smoother.update(first);

    auto overflow = trackAt(6U, 2'000'000'000LL);
    overflow.range = range(3.0, 1.0);
    EXPECT_THROW(smoother.update(overflow), audition::Error);
    EXPECT_EQ(smoother.trackedCount(), 1U);

    auto recovered = trackAt(6U, 0);
    recovered.range = range(3.0, 1.0);
    EXPECT_NO_THROW(smoother.update(recovered));
    ASSERT_TRUE(recovered.range.has_value());
    EXPECT_DOUBLE_EQ(recovered.range->distance_m.mean, 1.7);
}

TEST(TemporalSpatialTrackSmoother, AcceptsNearlySymmetricCovarianceAndCanonicalizes) {
    audition::TemporalSpatialSmootherOptions options{};
    options.position_measurement_weight = 0.5;
    audition::TemporalSpatialTrackSmoother smoother{options};
    auto first = trackAt(7U, 0);
    auto covariance = diagonalCovariance(1.0, 1.0, 1.0);
    covariance[1] = 0.2;
    covariance[3] = 0.2 + 1.0e-12;
    first.position = position(0.0, 0.0, 0.0, covariance);
    smoother.update(first);
    auto second = trackAt(7U, 1);
    second.position = position(0.0, 0.0, 0.0,
                               diagonalCovariance(1.0, 1.0, 1.0));
    smoother.update(second);
    ASSERT_TRUE(second.position.has_value());
    EXPECT_NEAR(second.position->covariance_m2[1], 0.1, 1.0e-10);
    EXPECT_DOUBLE_EQ(second.position->covariance_m2[1],
                     second.position->covariance_m2[3]);
}
