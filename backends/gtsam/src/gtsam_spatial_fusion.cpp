#include <audition/backends/gtsam/spatial_fusion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

#include <gtsam/geometry/Point3.h>
#include <gtsam/geometry/Unit3.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/LevenbergMarquardtParams.h>
#include <gtsam/nonlinear/Marginals.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam/nonlinear/Values.h>
#include <gtsam/sam/BearingFactor.h>
#include <gtsam/sam/RangeFactor.h>
#include <gtsam/slam/PriorFactor.h>

#include <audition/core/error.hpp>

namespace audition {
namespace {

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finiteVec(const Vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] double clampSigma(double sigma, double minimum_sigma) {
    if (!finite(sigma) || sigma < 0.0) {
        throw Error{ErrorCode::InvalidArgument, "Fusion sigma must be finite and non-negative"};
    }
    return std::max(sigma, minimum_sigma);
}

[[nodiscard]] double sigmaFromVariance(
    std::optional<double> variance,
    double fallback_sigma,
    double minimum_sigma) {
    if (!variance.has_value()) {
        return clampSigma(fallback_sigma, minimum_sigma);
    }
    if (!finite(*variance) || *variance < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Bearing variance must be finite and non-negative"};
    }
    return clampSigma(std::sqrt(*variance), minimum_sigma);
}

[[nodiscard]] Vec3 rotateLocalToWorld(const Quaternion& quaternion, const Vec3& local) {
    if (!finite(quaternion.w) || !finite(quaternion.x) ||
        !finite(quaternion.y) || !finite(quaternion.z) || !finiteVec(local)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Sensor orientation and bearing must contain only finite values"};
    }

    const double norm = std::sqrt(
        quaternion.w * quaternion.w +
        quaternion.x * quaternion.x +
        quaternion.y * quaternion.y +
        quaternion.z * quaternion.z);
    if (!finite(norm) || norm <= 1e-12) {
        throw Error{ErrorCode::InvalidArgument, "Sensor quaternion must be non-zero"};
    }

    const double w = quaternion.w / norm;
    const Vec3 q{
        quaternion.x / norm,
        quaternion.y / norm,
        quaternion.z / norm};

    const Vec3 t{
        2.0 * (q.y * local.z - q.z * local.y),
        2.0 * (q.z * local.x - q.x * local.z),
        2.0 * (q.x * local.y - q.y * local.x)};

    return Vec3{
        local.x + w * t.x + (q.y * t.z - q.z * t.y),
        local.y + w * t.y + (q.z * t.x - q.x * t.z),
        local.z + w * t.z + (q.x * t.y - q.y * t.x)};
}

[[nodiscard]] gtsam::Point3 toPoint(const Vec3& value) {
    if (!finiteVec(value)) {
        throw Error{ErrorCode::InvalidArgument, "Position must contain only finite values"};
    }
    return gtsam::Point3{value.x, value.y, value.z};
}

[[nodiscard]] gtsam::SharedNoiseModel positionNoise(
    const Covariance3& covariance,
    double minimum_sigma) {
    gtsam::Matrix33 matrix;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            const double value = covariance[row * 3 + column];
            if (!finite(value)) {
                throw Error{
                    ErrorCode::InvalidArgument,
                    "Position covariance must contain only finite values"};
            }
            matrix(
                static_cast<Eigen::Index>(row),
                static_cast<Eigen::Index>(column)) = value;
        }
    }

    matrix = 0.5 * (matrix + matrix.transpose());
    const double min_variance = minimum_sigma * minimum_sigma;
    for (Eigen::Index index = 0; index < 3; ++index) {
        matrix(index, index) = std::max(matrix(index, index), min_variance);
    }

    return gtsam::noiseModel::Gaussian::Covariance(matrix);
}

void checkClock(
    const Timestamp& timestamp,
    std::optional<ClockIdentity>& expected_clock) {
    if (!expected_clock.has_value()) {
        expected_clock = timestamp.clock();
        return;
    }
    if (timestamp.clock() != *expected_clock) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "Spatial fusion observations must use the same clock identity"};
    }
}

void validateOptions(const GtsamSpatialFusionOptions& options) {
    const double numeric_options[] = {
        options.default_bearing_sigma_rad,
        options.default_range_sigma_m,
        options.sensor_position_sigma_m,
        options.minimum_sigma,
        options.fallback_initial_range_m};

    for (const double value : numeric_options) {
        if (!finite(value) || value <= 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "GTSAM fusion numeric options must be finite and positive"};
        }
    }

    if (options.max_iterations == 0 ||
        options.max_iterations >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw Error{
            ErrorCode::InvalidArgument,
            "GTSAM fusion max_iterations must fit in a positive int"};
    }
}

[[nodiscard]] Vec3 initialGuess(
    const SpatialFusionInput& input,
    const GtsamSpatialFusionOptions& options) {
    if (!input.positions.empty()) {
        return input.positions[0].estimate.mean_m;
    }

    if (!input.bearings.empty()) {
        double distance = options.fallback_initial_range_m;
        if (!input.ranges.empty() &&
            input.ranges[0].distance_m.valid() &&
            input.ranges[0].distance_m.mean > 0.0) {
            distance = input.ranges[0].distance_m.mean;
        }

        const auto& bearing = input.bearings[0];
        const Vec3 world_direction = Direction3D::fromVector(
            rotateLocalToWorld(
                bearing.sensor_pose.orientation,
                bearing.bearing.direction.vector()))
                                         .vector();
        return Vec3{
            bearing.sensor_pose.position.x + world_direction.x * distance,
            bearing.sensor_pose.position.y + world_direction.y * distance,
            bearing.sensor_pose.position.z + world_direction.z * distance};
    }

    if (!input.ranges.empty()) {
        const auto& range = input.ranges[0];
        if (!range.distance_m.valid() || range.distance_m.mean < 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Range observation must have finite non-negative distance and variance"};
        }
        return Vec3{
            range.sensor_pose.position.x + range.distance_m.mean,
            range.sensor_pose.position.y,
            range.sensor_pose.position.z};
    }

    return {};
}

}  // namespace

GtsamSpatialFusion::GtsamSpatialFusion(GtsamSpatialFusionOptions options)
    : options_(options) {
    validateOptions(options_);
}

BackendInfo GtsamSpatialFusion::backendInfo() const {
    return BackendInfo{"gtsam", "4.2"};
}

std::optional<PositionEstimate> GtsamSpatialFusion::fuse(
    const SpatialFusionInput& input) const {
    if (input.bearings.empty() && input.ranges.empty() && input.positions.empty()) {
        return std::nullopt;
    }

    std::optional<ClockIdentity> expected_clock;
    for (const auto& observation : input.bearings) {
        checkClock(observation.timestamp, expected_clock);
    }
    for (const auto& observation : input.ranges) {
        checkClock(observation.timestamp, expected_clock);
        if (!observation.distance_m.valid() || observation.distance_m.mean < 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Range observation must have finite non-negative distance and variance"};
        }
        if (!finiteVec(observation.sensor_pose.position)) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Range sensor pose must contain only finite position values"};
        }
    }
    for (const auto& observation : input.positions) {
        checkClock(observation.timestamp, expected_clock);
        if (!finiteVec(observation.estimate.mean_m)) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Position observation must contain only finite coordinates"};
        }
    }

    const gtsam::Key source_key = gtsam::Symbol{'x', 0};
    gtsam::NonlinearFactorGraph graph;
    gtsam::Values initial;
    const Vec3 guess = initialGuess(input, options_);
    initial.insert(source_key, toPoint(guess));

    std::size_t sensor_index = 0;
    const auto addSensor = [&](const Pose3D& sensor_pose,
                               gtsam::NonlinearFactorGraph& target_graph,
                               gtsam::Values& target_initial,
                               std::size_t index) -> gtsam::Key {
        const gtsam::Key sensor_key = gtsam::Symbol{'s', index};
        const gtsam::Point3 sensor_point = toPoint(sensor_pose.position);
        target_initial.insert(sensor_key, sensor_point);
        target_graph.emplace_shared<gtsam::PriorFactor<gtsam::Point3>>(
            sensor_key,
            sensor_point,
            gtsam::noiseModel::Isotropic::Sigma(
                3,
                clampSigma(
                    options_.sensor_position_sigma_m,
                    options_.minimum_sigma)));
        return sensor_key;
    };

    for (const auto& observation : input.bearings) {
        const gtsam::Key sensor_key =
            addSensor(observation.sensor_pose, graph, initial, sensor_index++);
        const Vec3 world_direction = Direction3D::fromVector(
            rotateLocalToWorld(
                observation.sensor_pose.orientation,
                observation.bearing.direction.vector()))
                                         .vector();

        graph.emplace_shared<
            gtsam::BearingFactor<gtsam::Point3, gtsam::Point3>>(
            sensor_key,
            source_key,
            gtsam::Unit3{gtsam::Point3{
                world_direction.x,
                world_direction.y,
                world_direction.z}},
            gtsam::noiseModel::Isotropic::Sigma(
                2,
                sigmaFromVariance(
                    observation.bearing.angular_variance_rad2,
                    options_.default_bearing_sigma_rad,
                    options_.minimum_sigma)));
    }

    for (const auto& observation : input.ranges) {
        const gtsam::Key sensor_key =
            addSensor(observation.sensor_pose, graph, initial, sensor_index++);
        const double sigma = clampSigma(
            std::sqrt(observation.distance_m.variance),
            options_.minimum_sigma);
        const double effective_sigma =
            observation.distance_m.variance > 0.0
                ? sigma
                : clampSigma(
                      options_.default_range_sigma_m,
                      options_.minimum_sigma);

        graph.emplace_shared<
            gtsam::RangeFactor<gtsam::Point3, gtsam::Point3>>(
            sensor_key,
            source_key,
            observation.distance_m.mean,
            gtsam::noiseModel::Isotropic::Sigma(1, effective_sigma));
    }

    for (const auto& observation : input.positions) {
        graph.emplace_shared<gtsam::PriorFactor<gtsam::Point3>>(
            source_key,
            toPoint(observation.estimate.mean_m),
            positionNoise(
                observation.estimate.covariance_m2,
                options_.minimum_sigma));
    }

    try {
        gtsam::LevenbergMarquardtParams params;
        params.setMaxIterations(static_cast<int>(options_.max_iterations));
        params.setVerbosityLM("SILENT");

        const gtsam::Values result =
            gtsam::LevenbergMarquardtOptimizer{graph, initial, params}.optimize();
        const gtsam::Point3 point = result.at<gtsam::Point3>(source_key);

        const gtsam::Marginals marginals{graph, result};
        const gtsam::Matrix covariance = marginals.marginalCovariance(source_key);
        if (covariance.rows() != 3 || covariance.cols() != 3) {
            return std::nullopt;
        }

        PositionEstimate estimate{};
        estimate.mean_m = Vec3{point.x(), point.y(), point.z()};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                const double value = covariance(
                    static_cast<Eigen::Index>(row),
                    static_cast<Eigen::Index>(column));
                if (!finite(value)) {
                    return std::nullopt;
                }
                estimate.covariance_m2[row * 3 + column] = value;
            }
        }

        if (!finiteVec(estimate.mean_m)) {
            return std::nullopt;
        }

        estimate.confidence = Probability::zero();
        return estimate;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace audition
