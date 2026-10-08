#include <audition/backends/gtsam/spatial_fusion.hpp>

#include <array>
#include <cmath>

#include <gtest/gtest.h>

namespace {

audition::Timestamp timestamp(
    std::int64_t nanoseconds = 0,
    audition::ClockIdentity clock = {
        audition::ClockDomain::Monotonic,
        0}) {
    return audition::Timestamp{nanoseconds, clock};
}

audition::Covariance3 diagonalCovariance(double variance) {
    return audition::Covariance3{
        variance, 0.0, 0.0,
        0.0, variance, 0.0,
        0.0, 0.0, variance};
}

}  // namespace

TEST(GtsamSpatialFusion, ReportsBackendInfo) {
    const audition::GtsamSpatialFusion fusion;
    const auto info = fusion.backendInfo();
    EXPECT_EQ(info.name, "gtsam");
    EXPECT_EQ(info.implementation_version, "4.2");
}

TEST(GtsamSpatialFusion, FusesBearingAndRangeFromOneSensor) {
    audition::BearingObservation bearings[] = {
        {
            timestamp(),
            {},
            {
                audition::Direction3D::fromVector({1.0, 0.0, 0.0}),
                1e-6,
                audition::Probability::one(),
            },
        },
    };

    audition::ScalarRangeObservation ranges[] = {
        {
            timestamp(),
            {2.0, 1e-6},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Radar,
            {},
        },
    };

    const audition::SpatialFusionInput input{
        bearings,
        ranges,
        {},
    };

    const audition::GtsamSpatialFusion fusion;
    const auto result = fusion.fuse(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->mean_m.x, 2.0, 1e-3);
    EXPECT_NEAR(result->mean_m.y, 0.0, 1e-3);
    EXPECT_NEAR(result->mean_m.z, 0.0, 1e-3);
    EXPECT_DOUBLE_EQ(result->confidence.value(), 0.0);
}

TEST(GtsamSpatialFusion, AppliesSensorOrientationToLocalBearing) {
    constexpr double kHalfSqrtTwo = 0.7071067811865475244;

    audition::Pose3D sensor_pose{};
    sensor_pose.orientation = {
        kHalfSqrtTwo,
        0.0,
        0.0,
        kHalfSqrtTwo,
    };

    audition::BearingObservation bearings[] = {
        {
            timestamp(),
            sensor_pose,
            {
                audition::Direction3D::fromVector({1.0, 0.0, 0.0}),
                1e-6,
                audition::Probability::one(),
            },
        },
    };

    audition::ScalarRangeObservation ranges[] = {
        {
            timestamp(),
            {3.0, 1e-6},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Radar,
            sensor_pose,
        },
    };

    const audition::GtsamSpatialFusion fusion;
    const auto result = fusion.fuse({bearings, ranges, {}});
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->mean_m.x, 0.0, 1e-3);
    EXPECT_NEAR(result->mean_m.y, 3.0, 1e-3);
    EXPECT_NEAR(result->mean_m.z, 0.0, 1e-3);
}

TEST(GtsamSpatialFusion, UsesPositionObservationAsWorldFramePrior) {
    audition::PositionObservation positions[] = {
        {
            timestamp(),
            {
                {1.0, -2.0, 0.5},
                diagonalCovariance(0.04),
                audition::Probability::one(),
            },
        },
    };

    const audition::GtsamSpatialFusion fusion;
    const auto result = fusion.fuse({{}, {}, positions});
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->mean_m.x, 1.0, 1e-9);
    EXPECT_NEAR(result->mean_m.y, -2.0, 1e-9);
    EXPECT_NEAR(result->mean_m.z, 0.5, 1e-9);
    EXPECT_NEAR(result->covariance_m2[0], 0.04, 1e-6);
    EXPECT_NEAR(result->covariance_m2[4], 0.04, 1e-6);
    EXPECT_NEAR(result->covariance_m2[8], 0.04, 1e-6);
}

TEST(GtsamSpatialFusion, RejectsMixedClockIdentities) {
    audition::BearingObservation bearings[] = {
        {
            timestamp(
                0,
                {audition::ClockDomain::Monotonic, 1}),
            {},
            {},
        },
    };
    audition::ScalarRangeObservation ranges[] = {
        {
            timestamp(
                0,
                {audition::ClockDomain::Monotonic, 2}),
            {1.0, 0.01},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Radar,
            {},
        },
    };

    const audition::GtsamSpatialFusion fusion;
    try {
        static_cast<void>(fusion.fuse({bearings, ranges, {}}));
        FAIL() << "Expected clock-domain mismatch";
    } catch (const audition::Error& error) {
        EXPECT_EQ(error.code(), audition::ErrorCode::ClockDomainMismatch);
    }
}

TEST(GtsamSpatialFusion, EmptyInputReturnsNoEstimate) {
    const audition::GtsamSpatialFusion fusion;
    EXPECT_FALSE(fusion.fuse({}).has_value());
}
