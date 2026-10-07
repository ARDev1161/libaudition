#include <acoustic/core/geometry.hpp>

#include <gtest/gtest.h>

TEST(Probability, ValidatesRange) {
    EXPECT_DOUBLE_EQ(acoustic::Probability::from(0.25).value(), 0.25);
    EXPECT_THROW(acoustic::Probability::from(-0.1), acoustic::Error);
    EXPECT_THROW(acoustic::Probability::from(1.1), acoustic::Error);
}

TEST(Direction, NormalizesVector) {
    const auto direction = acoustic::Direction3D::fromVector({3.0, 4.0, 0.0});
    EXPECT_NEAR(direction.vector().x, 0.6, 1e-12);
    EXPECT_NEAR(direction.vector().y, 0.8, 1e-12);
}
