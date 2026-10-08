#include <audition/core/geometry.hpp>

#include <gtest/gtest.h>

TEST(Probability, ValidatesRange) {
    EXPECT_DOUBLE_EQ(audition::Probability::from(0.25).value(), 0.25);
    EXPECT_THROW(audition::Probability::from(-0.1), audition::Error);
    EXPECT_THROW(audition::Probability::from(1.1), audition::Error);
}

TEST(Direction, NormalizesVector) {
    const auto direction = audition::Direction3D::fromVector({3.0, 4.0, 0.0});
    EXPECT_NEAR(direction.vector().x, 0.6, 1e-12);
    EXPECT_NEAR(direction.vector().y, 0.8, 1e-12);
}

TEST(DirectionEstimate, UnknownDirectionVarianceIsRepresentable) {
    audition::DirectionEstimate estimate{};
    EXPECT_FALSE(estimate.angular_variance_rad2.has_value());
}


TEST(PositionEstimate, ConfidenceMayBeUnknown) {
    audition::PositionEstimate estimate{};
    EXPECT_FALSE(estimate.confidence.has_value());
}
