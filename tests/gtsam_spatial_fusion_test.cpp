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

audition::Pose3D poseAt(double x, double y, double z) {
    audition::Pose3D pose{};
    pose.position = {x, y, z};
    return pose;
}

audition::Direction3D directionFromTo(
    const audition::Vec3& origin,
    const audition::Vec3& target) {
    return audition::Direction3D::fromVector({
        target.x - origin.x,
        target.y - origin.y,
        target.z - origin.z});
}

double distance(
    const audition::Vec3& a,
    const audition::Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
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
            audition::RangeEstimate::Method::Unknown,
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
            audition::RangeEstimate::Method::Unknown,
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
            audition::RangeEstimate::Method::Unknown,
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


TEST(GtsamSpatialFusion, TriangulatesSourceFromMultipleBearings) {
    const audition::Vec3 target{2.0, 1.0, 0.5};
    const auto pose_a = poseAt(0.0, 0.0, 0.0);
    const auto pose_b = poseAt(0.0, 2.0, 0.0);

    audition::BearingObservation bearings[] = {
        {
            timestamp(20),
            pose_a,
            {
                directionFromTo(pose_a.position, target),
                1e-6,
                audition::Probability::one(),
            },
        },
        {
            timestamp(10),
            pose_b,
            {
                directionFromTo(pose_b.position, target),
                1e-6,
                audition::Probability::one(),
            },
        },
    };

    const audition::GtsamSpatialFusion fusion;
    const auto result = fusion.fuse({bearings, {}, {}});
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->mean_m.x, target.x, 1e-3);
    EXPECT_NEAR(result->mean_m.y, target.y, 1e-3);
    EXPECT_NEAR(result->mean_m.z, target.z, 1e-3);
}

TEST(GtsamSpatialFusion, RejectsDegenerateParallelBearingGeometry) {
    audition::BearingObservation bearings[] = {
        {
            timestamp(),
            poseAt(0.0, 0.0, 0.0),
            {
                audition::Direction3D::fromVector({1.0, 0.0, 0.0}),
                1e-6,
                audition::Probability::one(),
            },
        },
        {
            timestamp(),
            poseAt(0.0, 1.0, 0.0),
            {
                audition::Direction3D::fromVector({1.0, 0.0, 0.0}),
                1e-6,
                audition::Probability::one(),
            },
        },
    };

    const audition::GtsamSpatialFusion fusion;
    EXPECT_FALSE(fusion.fuse({bearings, {}, {}}).has_value());
}

TEST(GtsamSpatialFusion, TrilateratesFromNonCoplanarRanges) {
    const audition::Vec3 target{1.0, 1.0, 1.0};
    const audition::Pose3D poses[] = {
        poseAt(0.0, 0.0, 0.0),
        poseAt(3.0, 0.0, 0.0),
        poseAt(0.0, 3.0, 0.0),
        poseAt(0.0, 0.0, 3.0),
    };

    audition::ScalarRangeObservation ranges[] = {
        {
            timestamp(),
            {distance(poses[0].position, target), 1e-6},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poses[0],
        },
        {
            timestamp(),
            {distance(poses[1].position, target), 1e-6},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poses[1],
        },
        {
            timestamp(),
            {distance(poses[2].position, target), 1e-6},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poses[2],
        },
        {
            timestamp(),
            {distance(poses[3].position, target), 1e-6},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poses[3],
        },
    };

    const audition::GtsamSpatialFusion fusion;
    const auto result = fusion.fuse({{}, ranges, {}});
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->mean_m.x, target.x, 1e-3);
    EXPECT_NEAR(result->mean_m.y, target.y, 1e-3);
    EXPECT_NEAR(result->mean_m.z, target.z, 1e-3);
}

TEST(GtsamSpatialFusion, RejectsRangeOnlyGeometryWithout3DAnchor) {
    audition::ScalarRangeObservation ranges[] = {
        {
            timestamp(),
            {1.0, 0.01},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poseAt(0.0, 0.0, 0.0),
        },
        {
            timestamp(),
            {1.0, 0.01},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poseAt(2.0, 0.0, 0.0),
        },
        {
            timestamp(),
            {1.0, 0.01},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poseAt(0.0, 2.0, 0.0),
        },
        {
            timestamp(),
            {1.0, 0.01},
            audition::Probability::one(),
            audition::RangeEstimate::Method::Unknown,
            poseAt(2.0, 2.0, 0.0),
        },
    };

    const audition::GtsamSpatialFusion fusion;
    EXPECT_FALSE(fusion.fuse({{}, ranges, {}}).has_value());
}

TEST(GtsamSpatialFusion, HuberLossSuppressesConflictingPositionOutlier) {
    audition::PositionObservation positions[] = {
        {
            timestamp(),
            {
                {1.0, 2.0, 0.5},
                diagonalCovariance(0.01),
                audition::Probability::one(),
            },
        },
        {
            timestamp(),
            {
                {1.02, 1.98, 0.51},
                diagonalCovariance(0.01),
                audition::Probability::one(),
            },
        },
        {
            timestamp(),
            {
                {0.98, 2.01, 0.49},
                diagonalCovariance(0.01),
                audition::Probability::one(),
            },
        },
        {
            timestamp(),
            {
                {1.01, 2.02, 0.50},
                diagonalCovariance(0.01),
                audition::Probability::one(),
            },
        },
        {
            timestamp(),
            {
                {20.0, -15.0, 7.0},
                diagonalCovariance(0.01),
                audition::Probability::one(),
            },
        },
    };

    audition::GtsamSpatialFusionOptions options{};
    options.enable_huber_loss = true;
    options.huber_k = 1.0;

    const audition::GtsamSpatialFusion fusion{options};
    const auto result = fusion.fuse({{}, {}, positions});
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->mean_m.x, 1.0, 0.15);
    EXPECT_NEAR(result->mean_m.y, 2.0, 0.15);
    EXPECT_NEAR(result->mean_m.z, 0.5, 0.15);
}

TEST(GtsamSpatialFusion, RejectsIndefinitePositionCovariance) {
    audition::PositionObservation positions[] = {
        {
            timestamp(),
            {
                {1.0, 2.0, 3.0},
                {
                    1.0, 2.0, 0.0,
                    2.0, 1.0, 0.0,
                    0.0, 0.0, 1.0,
                },
                audition::Probability::one(),
            },
        },
    };

    const audition::GtsamSpatialFusion fusion;
    try {
        static_cast<void>(fusion.fuse({{}, {}, positions}));
        FAIL() << "Expected invalid covariance error";
    } catch (const audition::Error& error) {
        EXPECT_EQ(error.code(), audition::ErrorCode::InvalidArgument);
    }
}
