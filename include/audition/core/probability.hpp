#pragma once

#include <array>
#include <cmath>

#include <audition/core/error.hpp>

namespace audition {

class Probability {
public:
    constexpr Probability() noexcept = default;

    static Probability from(double value) {
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
            throw Error{ErrorCode::InvalidArgument, "Probability must be finite and in [0, 1]"};
        }
        return Probability{value};
    }

    [[nodiscard]] static constexpr Probability zero() noexcept { return Probability{0.0}; }
    [[nodiscard]] static constexpr Probability one() noexcept { return Probability{1.0}; }
    [[nodiscard]] constexpr double value() const noexcept { return value_; }

private:
    explicit constexpr Probability(double value) noexcept : value_(value) {}
    double value_{0.0};
};

struct Score {
    double value{0.0};
};

struct Gaussian1D {
    double mean{0.0};
    double variance{0.0};

    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(mean) && std::isfinite(variance) && variance >= 0.0;
    }
};

using Covariance3 = std::array<double, 9>;

}  // namespace audition
