#include <audition/backends/gtsam/spatial_fusion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

#include <audition/core/error.hpp>

#include <gtsam/config.h>
#include <gtsam/geometry/Point3.h>
#include <gtsam/geometry/Rot3.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/linear/NoiseModel.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/Marginals.h>
#include <gtsam/nonlinear/NoiseModelFactorN.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam/nonlinear/PriorFactor.h>
#include <gtsam/nonlinear/Values.h>

#include <Eigen/Eigenvalues>

namespace audition {
namespace {

constexpr double kMinimumDirectionNorm = 1e-9;
constexpr double kCovarianceSymmetryTolerance = 1e-9;
const gtsam::Key kSourceKey = gtsam::Symbol{'s', 0U};

void requireConfiguration(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

bool finite(double value) noexcept {
    return std::isfinite(value);
}

bool finite(const Vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

void validatePose(const Pose3D& pose) {
    if (!finite(pose.position) || !finite(pose.orientation.w) ||
        !finite(pose.orientation.x) || !finite(pose.orientation.y) ||
        !finite(pose.orientation.z)) {
        throw Error{ErrorCode::InvalidArgument,
                    "GTSAM fusion received a non-finite sensor pose"};
    }

    const double squared_norm = pose.orientation.w * pose.orientation.w +
                                pose.orientation.x * pose.orientation.x +
                                pose.orientation.y * pose.orientation.y +
                                pose.orientation.z * pose.orientation.z;
    if (!finite(squared_norm) ||
        squared_norm <= kMinimumDirectionNorm * kMinimumDirectionNorm) {
        throw Error{ErrorCode::InvalidArgument,
                    "GTSAM fusion sensor quaternion must be non-zero"};
    }
}

gtsam::Point3 toPoint(const Vec3& value) {
    if (!finite(value)) {
        throw Error{ErrorCode::InvalidArgument,
                    "GTSAM fusion received a non-finite position"};
    }
    return gtsam::Point3{value.x, value.y, value.z};
}

gtsam::Rot3 toRotation(const Quaternion& quaternion) {
    const double norm = std::sqrt(quaternion.w * quaternion.w +
                                  quaternion.x * quaternion.x +
                                  quaternion.y * quaternion.y +
                                  quaternion.z * quaternion.z);
    return gtsam::Rot3::Quaternion(quaternion.w / norm,
                                   quaternion.x / norm,
                                   quaternion.y / norm,
                                   quaternion.z / norm);
}

gtsam::Vector3 toVector(const Direction3D& direction) {
    const auto& value = direction.vector();
    return gtsam::Vector3{value.x, value.y, value.z};
}

gtsam::Matrix3 positionCovariance(
    const PositionEstimate& estimate,
    const GtsamSpatialFusionOptions& options) {
    gtsam::Matrix3 covariance;
    covariance << estimate.covariance_m2[0], estimate.covariance_m2[1],
        estimate.covariance_m2[2], estimate.covariance_m2[3],
        estimate.covariance_m2[4], estimate.covariance_m2[5],
        estimate.covariance_m2[6], estimate.covariance_m2[7],
        estimate.covariance_m2[8];

    if (!covariance.allFinite()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "GTSAM fusion position covariance contains a non-finite value"};
    }
    if (!covariance.isApprox(
            covariance.transpose(), kCovarianceSymmetryTolerance)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "GTSAM fusion position covariance must be symmetric"};
    }

    Eigen::SelfAdjointEigenSolver<gtsam::Matrix3> solver{covariance};
    if (solver.info() != Eigen::Success) {
        throw Error{
            ErrorCode::InvalidArgument,
            "GTSAM fusion position covariance eigendecomposition failed"};
    }

    const double minimum_eigenvalue =
        solver.eigenvalues().minCoeff();
    if (minimum_eigenvalue < -kCovarianceSymmetryTolerance) {
        throw Error{
            ErrorCode::InvalidArgument,
            "GTSAM fusion position covariance must be positive semidefinite"};
    }

    const double minimum_variance =
        options.minimum_position_sigma_m *
        options.minimum_position_sigma_m;
    if (minimum_eigenvalue < minimum_variance) {
        covariance.diagonal().array() +=
            minimum_variance - minimum_eigenvalue;
    }
    return covariance;
}

class BearingPointFactor final
    : public gtsam::NoiseModelFactorN<gtsam::Point3> {
public:
    BearingPointFactor(
        gtsam::Key key,
        gtsam::Point3 sensor_position,
        gtsam::Rot3 sensor_rotation,
        gtsam::Vector3 measured_local_direction,
        const gtsam::SharedNoiseModel& noise_model)
        : gtsam::NoiseModelFactorN<gtsam::Point3>(
              noise_model, key),
          sensor_position_(std::move(sensor_position)),
          sensor_rotation_(std::move(sensor_rotation)),
          measured_local_direction_(
              std::move(measured_local_direction)) {}

    gtsam::Vector evaluateError(
        const gtsam::Point3& source,
        gtsam::OptionalMatrixType H) const override {
        const gtsam::Vector3 world_delta =
            source - sensor_position_;
        const gtsam::Vector3 local_delta =
            sensor_rotation_.unrotate(world_delta);
        const double distance = local_delta.norm();

        if (!finite(distance) ||
            distance <= kMinimumDirectionNorm) {
            if (H != nullptr) {
                *H = gtsam::Matrix33::Zero();
            }
            return gtsam::Vector3::Zero();
        }

        const gtsam::Vector3 predicted =
            local_delta / distance;
        if (H != nullptr) {
            const gtsam::Matrix3 normalization =
                (gtsam::Matrix3::Identity() -
                 predicted * predicted.transpose()) /
                distance;
            *H = normalization *
                 sensor_rotation_.transpose();
        }
        return predicted - measured_local_direction_;
    }

    gtsam::NonlinearFactor::shared_ptr clone()
        const override {
        return std::make_shared<BearingPointFactor>(*this);
    }

private:
    gtsam::Point3 sensor_position_;
    gtsam::Rot3 sensor_rotation_;
    gtsam::Vector3 measured_local_direction_;
};

class RangePointFactor final
    : public gtsam::NoiseModelFactorN<gtsam::Point3> {
public:
    RangePointFactor(
        gtsam::Key key,
        gtsam::Point3 sensor_position,
        double measured_distance_m,
        const gtsam::SharedNoiseModel& noise_model)
        : gtsam::NoiseModelFactorN<gtsam::Point3>(
              noise_model, key),
          sensor_position_(std::move(sensor_position)),
          measured_distance_m_(measured_distance_m) {}

    gtsam::Vector evaluateError(
        const gtsam::Point3& source,
        gtsam::OptionalMatrixType H) const override {
        const gtsam::Vector3 delta =
            source - sensor_position_;
        const double distance = delta.norm();

        if (H != nullptr) {
            if (distance <= kMinimumDirectionNorm) {
                *H = gtsam::Matrix13::Zero();
            } else {
                *H = delta.transpose() / distance;
            }
        }
        return gtsam::Vector1{
            distance - measured_distance_m_};
    }

    gtsam::NonlinearFactor::shared_ptr clone()
        const override {
        return std::make_shared<RangePointFactor>(*this);
    }

private:
    gtsam::Point3 sensor_position_;
    double measured_distance_m_{0.0};
};

void addBearingFactor(
    gtsam::NonlinearFactorGraph& graph,
    const BearingObservation& observation,
    const GtsamSpatialFusionOptions& options) {
    validatePose(observation.sensor_pose);
    const auto direction =
        toVector(observation.bearing.direction);

    double sigma_rad =
        options.default_bearing_sigma_rad;
    if (observation.bearing.angular_variance_rad2
            .has_value()) {
        const double variance =
            *observation.bearing.angular_variance_rad2;
        if (!finite(variance) || variance < 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "GTSAM fusion bearing variance must be "
                "finite and non-negative"};
        }
        sigma_rad = std::sqrt(variance);
    }
    sigma_rad = std::max(
        sigma_rad, options.minimum_bearing_sigma_rad);

    graph.emplace_shared<BearingPointFactor>(
        kSourceKey,
        toPoint(observation.sensor_pose.position),
        toRotation(observation.sensor_pose.orientation),
        direction,
        gtsam::noiseModel::Isotropic::Sigma(
            3U, sigma_rad));
}

void addRangeFactor(
    gtsam::NonlinearFactorGraph& graph,
    const ScalarRangeObservation& observation,
    const GtsamSpatialFusionOptions& options) {
    validatePose(observation.sensor_pose);
    if (!observation.distance_m.valid() ||
        observation.distance_m.mean < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "GTSAM fusion range must have a finite "
            "non-negative mean and variance"};
    }

    const double sigma_m =
        std::max(
            std::sqrt(observation.distance_m.variance),
            options.minimum_range_sigma_m);
    graph.emplace_shared<RangePointFactor>(
        kSourceKey,
        toPoint(observation.sensor_pose.position),
        observation.distance_m.mean,
        gtsam::noiseModel::Isotropic::Sigma(
            1U, sigma_m));
}

void addPositionFactor(
    gtsam::NonlinearFactorGraph& graph,
    const PositionObservation& observation,
    const GtsamSpatialFusionOptions& options) {
    const auto mean =
        toPoint(observation.estimate.mean_m);
    const auto covariance =
        positionCovariance(observation.estimate, options);
    graph.emplace_shared<
        gtsam::PriorFactor<gtsam::Point3>>(
        kSourceKey,
        mean,
        gtsam::noiseModel::Gaussian::Covariance(
            covariance));
}

gtsam::Point3 initialEstimate(
    const SpatialFusionInput& input,
    const GtsamSpatialFusionOptions& options) {
    if (!input.positions.empty()) {
        gtsam::Point3 sum{0.0, 0.0, 0.0};
        for (const auto& observation :
             input.positions) {
            sum += toPoint(
                observation.estimate.mean_m);
        }
        return sum /
               static_cast<double>(
                   input.positions.size());
    }

    double range_sum = 0.0;
    std::size_t valid_ranges = 0U;
    for (const auto& observation : input.ranges) {
        if (observation.distance_m.valid() &&
            observation.distance_m.mean >= 0.0) {
            range_sum += observation.distance_m.mean;
            ++valid_ranges;
        }
    }
    const double depth =
        valid_ranges == 0U
            ? options.initialization_depth_m
            : range_sum /
                  static_cast<double>(valid_ranges);

    if (!input.bearings.empty()) {
        gtsam::Point3 sum{0.0, 0.0, 0.0};
        for (const auto& observation :
             input.bearings) {
            validatePose(observation.sensor_pose);
            const auto anchor =
                toPoint(
                    observation.sensor_pose.position);
            const auto rotation =
                toRotation(
                    observation.sensor_pose.orientation);
            sum +=
                anchor +
                rotation.rotate(
                    toVector(
                        observation.bearing.direction)) *
                    depth;
        }
        return sum /
               static_cast<double>(
                   input.bearings.size());
    }

    gtsam::Point3 sensor_sum{0.0, 0.0, 0.0};
    for (const auto& observation : input.ranges) {
        validatePose(observation.sensor_pose);
        sensor_sum +=
            toPoint(observation.sensor_pose.position);
    }
    const auto centroid =
        sensor_sum /
        static_cast<double>(input.ranges.size());
    return centroid +
           gtsam::Point3{depth, 0.0, 0.0};
}

PositionEstimate toPositionEstimate(
    const gtsam::Point3& point,
    const gtsam::Matrix& covariance) {
    if (covariance.rows() != 3 ||
        covariance.cols() != 3 ||
        !covariance.allFinite()) {
        throw std::runtime_error{
            "GTSAM produced an invalid marginal covariance"};
    }

    PositionEstimate estimate{};
    estimate.mean_m =
        Vec3{point.x(), point.y(), point.z()};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U;
             column < 3U;
             ++column) {
            estimate.covariance_m2[
                row * 3U + column] =
                covariance(
                    static_cast<Eigen::Index>(row),
                    static_cast<Eigen::Index>(column));
        }
    }
    return estimate;
}

}  // namespace

void validateGtsamSpatialFusionOptions(
    const GtsamSpatialFusionOptions& options) {
    requireConfiguration(
        finite(options.default_bearing_sigma_rad) &&
            options.default_bearing_sigma_rad > 0.0,
        "GTSAM default bearing sigma must be "
        "finite and positive");
    requireConfiguration(
        finite(options.minimum_bearing_sigma_rad) &&
            options.minimum_bearing_sigma_rad > 0.0,
        "GTSAM minimum bearing sigma must be "
        "finite and positive");
    requireConfiguration(
        finite(options.minimum_range_sigma_m) &&
            options.minimum_range_sigma_m > 0.0,
        "GTSAM minimum range sigma must be "
        "finite and positive");
    requireConfiguration(
        finite(options.minimum_position_sigma_m) &&
            options.minimum_position_sigma_m > 0.0,
        "GTSAM minimum position sigma must be "
        "finite and positive");
    requireConfiguration(
        finite(options.initialization_depth_m) &&
            options.initialization_depth_m > 0.0,
        "GTSAM initialization depth must be "
        "finite and positive");
}

class GtsamSpatialFusion::Impl {
public:
    explicit Impl(
        GtsamSpatialFusionOptions options)
        : options_(std::move(options)) {
        validateGtsamSpatialFusionOptions(options_);
    }

    [[nodiscard]]
    std::optional<PositionEstimate> fuse(
        const SpatialFusionInput& input) const {
        if (input.bearings.empty() &&
            input.ranges.empty() &&
            input.positions.empty()) {
            return std::nullopt;
        }

        gtsam::NonlinearFactorGraph graph;
        for (const auto& observation :
             input.bearings) {
            addBearingFactor(
                graph, observation, options_);
        }
        for (const auto& observation :
             input.ranges) {
            addRangeFactor(
                graph, observation, options_);
        }
        for (const auto& observation :
             input.positions) {
            addPositionFactor(
                graph, observation, options_);
        }

        gtsam::Values initial;
        initial.insert(
            kSourceKey,
            initialEstimate(input, options_));

        try {
            const gtsam::Values result =
                gtsam::LevenbergMarquardtOptimizer{
                    graph, initial}
                    .optimize();
            const auto point =
                result.at<gtsam::Point3>(
                    kSourceKey);
            const gtsam::Marginals marginals{
                graph,
                result,
                gtsam::Marginals::QR};
            return toPositionEstimate(
                point,
                marginals.marginalCovariance(
                    kSourceKey));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    [[nodiscard]]
    const GtsamSpatialFusionOptions& options()
        const noexcept {
        return options_;
    }

private:
    GtsamSpatialFusionOptions options_{};
};

GtsamSpatialFusion::GtsamSpatialFusion(
    GtsamSpatialFusionOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(options))) {}

GtsamSpatialFusion::~GtsamSpatialFusion() =
    default;
GtsamSpatialFusion::GtsamSpatialFusion(
    GtsamSpatialFusion&&) noexcept = default;
GtsamSpatialFusion&
GtsamSpatialFusion::operator=(
    GtsamSpatialFusion&&) noexcept = default;

BackendInfo GtsamSpatialFusion::backendInfo() const {
    return BackendInfo{
        "gtsam", GTSAM_VERSION_STRING};
}

std::optional<PositionEstimate>
GtsamSpatialFusion::fuse(
    const SpatialFusionInput& input) const {
    return impl_->fuse(input);
}

const GtsamSpatialFusionOptions&
GtsamSpatialFusion::options() const noexcept {
    return impl_->options();
}

}  // namespace audition
