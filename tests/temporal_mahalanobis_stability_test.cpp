#include <audition/temporal/mahalanobis_gate.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
audition::temporal::DiagonalPositionEvidence evidence(
    std::int64_t time, double x, double variance) {
    return {{time, {audition::ClockDomain::Simulation, 23}},
            {x, 0.0, 0.0}, {variance, variance, variance}};
}
}

TEST(MahalanobisStability, ZeroVarianceUsesExplicitFloor) {
    audition::temporal::MahalanobisGateOptions opts{};
    opts.min_combined_variance = 0.25;
    audition::temporal::DiagonalMahalanobisGate gate(opts);
    const auto result = gate.evaluate(evidence(1, 0, 0), evidence(1, 1, 0));
    EXPECT_NEAR(result.squared_distance, 4.0, 1e-12);
    EXPECT_TRUE(result.compatible);
}

TEST(MahalanobisStability, ExtremeClockSpanDoesNotOverflow) {
    audition::temporal::DiagonalMahalanobisGate gate;
    const auto result = gate.evaluate(
        evidence(std::numeric_limits<std::int64_t>::min(), 0, 1),
        evidence(std::numeric_limits<std::int64_t>::max(), 0, 1));
    EXPECT_EQ(result.elapsed_nanoseconds, std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(result.compatible);
}

TEST(MahalanobisStability, OverflowingVarianceSumIsRejected) {
    audition::temporal::DiagonalMahalanobisGate gate;
    const double huge = std::numeric_limits<double>::max();
    EXPECT_THROW(static_cast<void>(gate.evaluate(evidence(0, 0, huge),
                                                  evidence(0, 1, huge))),
                 std::overflow_error);
}

TEST(MahalanobisStability, HugeCoordinateDifferenceIsRejected) {
    audition::temporal::DiagonalMahalanobisGate gate;
    const double huge = std::numeric_limits<double>::max();
    EXPECT_THROW(static_cast<void>(gate.evaluate(evidence(0, huge, 1),
                                                  evidence(0, -huge, 1))),
                 std::overflow_error);
}
