#pragma once
#include <audition/core/time.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace audition::temporal {

// Two independent Cartesian position estimates, in the SAME metric frame.
// Covariance is diagonal (metres squared). Cross-axis correlations are NOT
// supported; callers must not silently drop nonzero covariance off-diagonals.
struct DiagonalPositionEvidence {
    Timestamp time{};
    std::array<double,3> position{};
    std::array<double,3> variance{};
};
struct MahalanobisGateOptions {
    Duration max_elapsed{0};
    double max_squared_distance{7.814727903}; // illustrative chi-square 3DOF 95% gate
    double min_combined_variance{1e-12};
};
struct MahalanobisGateResult {
    bool compatible{false};
    double squared_distance{0};
    std::uint64_t elapsed_nanoseconds{0};
};

class DiagonalMahalanobisGate final {
public:
    explicit DiagonalMahalanobisGate(MahalanobisGateOptions o={}) : options_(o) {
        if (o.max_elapsed.nanoseconds()<0 || !std::isfinite(o.max_squared_distance) ||
            o.max_squared_distance<0 || !std::isfinite(o.min_combined_variance) ||
            o.min_combined_variance<=0)
            throw std::invalid_argument("invalid Mahalanobis gate options");
    }
    [[nodiscard]] MahalanobisGateResult evaluate(const DiagonalPositionEvidence& a,
                                                  const DiagonalPositionEvidence& b) const {
        if (!a.time.comparableWith(b.time)) throw std::invalid_argument("clock mismatch");
        const auto ta=a.time.nanoseconds(),tb=b.time.nanoseconds();
        const auto ua=static_cast<std::uint64_t>(ta),ub=static_cast<std::uint64_t>(tb);
        const std::uint64_t elapsed=ta>=tb?ua-ub:ub-ua;
        double squared=0;
        for (std::size_t i=0;i<3;++i) {
            if (!std::isfinite(a.position[i]) || !std::isfinite(b.position[i]) ||
                !std::isfinite(a.variance[i]) || !std::isfinite(b.variance[i]) ||
                a.variance[i]<0 || b.variance[i]<0)
                throw std::invalid_argument("invalid diagonal position evidence");
            const double v=a.variance[i]+b.variance[i];
            if (!std::isfinite(v)) throw std::overflow_error("covariance sum overflow");
            const double variance=std::max(v,options_.min_combined_variance);
            const double delta=a.position[i]-b.position[i];
            const double component=(delta/std::sqrt(variance));
            if (!std::isfinite(component)) throw std::overflow_error("distance overflow");
            squared+=component*component;
            if (!std::isfinite(squared)) throw std::overflow_error("squared distance overflow");
        }
        return {elapsed<=static_cast<std::uint64_t>(options_.max_elapsed.nanoseconds()) &&
                squared<=options_.max_squared_distance, squared, elapsed};
    }
private:
    MahalanobisGateOptions options_{};
};
} // namespace audition::temporal
