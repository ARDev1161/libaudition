#pragma once

#include <array>
#include <cmath>
#include <optional>

#include <audition/core/error.hpp>
#include <audition/core/probability.hpp>

namespace audition {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    [[nodiscard]] double squaredNorm() const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] double norm() const noexcept { return std::sqrt(squaredNorm()); }
};

class Direction3D {
public:
    static Direction3D fromVector(Vec3 value) {
        const double norm = value.norm();
        if (!std::isfinite(norm) || norm <= 1e-12) {
            throw Error{ErrorCode::InvalidArgument, "Direction vector must be finite and non-zero"};
        }
        return Direction3D{{value.x / norm, value.y / norm, value.z / norm}};
    }

    [[nodiscard]] constexpr const Vec3& vector() const noexcept { return value_; }

private:
    explicit constexpr Direction3D(Vec3 value) noexcept : value_(value) {}
    Vec3 value_{};
};

struct Quaternion {
    double w{1.0};
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct Pose3D {
    Vec3 position{};
    Quaternion orientation{};
};

struct DirectionEstimate {
    Direction3D direction{Direction3D::fromVector({1.0, 0.0, 0.0})};
    std::optional<double> angular_variance_rad2{};
    Probability confidence{Probability::one()};
};

struct RangeEstimate {
    enum class Method {
        Unknown,
        LevelPrior,
        BearingTriangulation,
        Vision,
        Radar,
        Fused,
    };

    Gaussian1D distance_m{};
    Probability confidence{Probability::zero()};
    Method method{Method::Unknown};
};

struct PositionEstimate {
    Vec3 mean_m{};
    Covariance3 covariance_m2{};
    Probability confidence{Probability::zero()};
};

}  // namespace audition
