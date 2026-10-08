#include <audition/backends/gtsam.hpp>

#include <type_traits>

#include <gtest/gtest.h>

namespace {

audition::Covariance3 diagonalCovariance(double variance) {
    return audition::Covariance3{
        variance, 0.0, 0.0,
        0.0, variance, 0.0,
        0.0, 0.0, variance};
}

}  // namespace

static_assert(std::is_base_of_v<
              audition::ISpatialFusion,
              audition::GtsamSpatialFusion>);

TEST(GtsamSpatialFusion, ValidatesOptions) {
    audition::GtsamSpatialFusionOptions options{};
    EXPECT_NO_THROW(
        audition::validateGtsamSpatialFusionOptions(
            options));

    options.minimum_range_sigma_m = 0.0;
    EXPECT_THROW(
        audition::validateGtsamSpatialFusionOptions(
            options),
        audition::Error);
}

TEST(GtsamSpatialFusion, EmptyInputHasNoEstimate) {
    audition::GtsamSpatialFusion fusion{};
    const audition::SpatialFusionInput input{};
    EXPECT_FALSE(fusion.fuse(input).has_value());
}

TEST(GtsamSpatialFusion, FusesPositionPrior) {
    audition::PositionObservation positions[1]{};
    positions[0].estimate.mean_m =
        audition::Vec3{1.0, -2.0, 0.5};
    positions[0].estimate.covariance_m2 =
        diagonalCovariance(0.04);

    audition::SpatialFusionInput input{};
    input.positions =
        audition::Span<
            const audition::PositionObservation>{
            positions, 1U};

    audition::GtsamSpatialFusion fusion{};
    const auto estimate = fusion.fuse(input);
    ASSERT_TRUE(estimate.has_value());
    EXPECT_NEAR(estimate->mean_m.x, 1.0, 1e-6);
    EXPECT_NEAR(estimate->mean_m.y, -2.0, 1e-6);
    EXPECT_NEAR(estimate->mean_m.z, 0.5, 1e-6);
    EXPECT_NEAR(
        estimate->covariance_m2[0], 0.04, 1e-6);
    EXPECT_FALSE(estimate->confidence.has_value());
}

TEST(
    GtsamSpatialFusion,
    FusesBearingAndRangeAtKnownSensorPose) {
    audition::BearingObservation bearings[1]{};
    bearings[0].sensor_pose.position =
        audition::Vec3{0.0, 0.0, 0.0};
    bearings[0].bearing.direction =
        audition::Direction3D::fromVector(
            {1.0, 0.0, 0.0});
    bearings[0].bearing.angular_variance_rad2 =
        1e-4;

    audition::ScalarRangeObservation ranges[1]{};
    ranges[0].sensor_pose.position =
        audition::Vec3{0.0, 0.0, 0.0};
    ranges[0].distance_m =
        audition::Gaussian1D{2.0, 1e-4};

    audition::SpatialFusionInput input{};
    input.bearings =
        audition::Span<
            const audition::BearingObservation>{
            bearings, 1U};
    input.ranges =
        audition::Span<
            const audition::ScalarRangeObservation>{
            ranges, 1U};

    audition::GtsamSpatialFusion fusion{};
    const auto estimate = fusion.fuse(input);
    ASSERT_TRUE(estimate.has_value());
    EXPECT_NEAR(estimate->mean_m.x, 2.0, 1e-4);
    EXPECT_NEAR(estimate->mean_m.y, 0.0, 1e-4);
    EXPECT_NEAR(estimate->mean_m.z, 0.0, 1e-4);
}

TEST(
    GtsamSpatialFusion,
    BearingOnlySingleViewIsUnderconstrained) {
    audition::BearingObservation bearings[1]{};
    bearings[0].bearing.direction =
        audition::Direction3D::fromVector(
            {1.0, 0.0, 0.0});
    bearings[0].bearing.angular_variance_rad2 =
        1e-4;

    audition::SpatialFusionInput input{};
    input.bearings =
        audition::Span<
            const audition::BearingObservation>{
            bearings, 1U};

    audition::GtsamSpatialFusion fusion{};
    EXPECT_FALSE(fusion.fuse(input).has_value());
}
