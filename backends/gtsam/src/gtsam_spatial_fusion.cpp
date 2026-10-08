#include <audition/backends/gtsam/spatial_fusion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <optional>

#include <Eigen/Eigenvalues>

#include <gtsam/base/Vector.h>
#include <gtsam/geometry/Point3.h>
#include <gtsam/geometry/Pose3.h>
#include <gtsam/geometry/Rot3.h>
#include <gtsam/geometry/Unit3.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/linear/NoiseModel.h>
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

[[nodiscard]] gtsam::Pose3 toPose(const Pose3D& value) {
    if (!finiteVec(value.position) ||
        !finite(value.orientation.w) ||
        !finite(value.orientation.x) ||
        !finite(value.orientation.y) ||
        !finite(value.orientation.z)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Sensor pose must contain only finite values"};
    }

    const double norm = std::sqrt(
        value.orientation.w * value.orientation.w +
        value.orientation.x * value.orientation.x +
        value.orientation.y * value.orientation.y +
        value.orientation.z * value.orientation.z);
    if (!finite(norm) || norm <= 1e-12) {
        throw Error{ErrorCode::InvalidArgument, "Sensor quaternion must be non-zero"};
    }

    return gtsam::Pose3{
        gtsam::Rot3::Quaternion(
            value.orientation.w / norm,
            value.orientation.x / norm,
            value.orientation.y / norm,
            value.orientation.z / norm),
        toPoint(value.position)};
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

    Eigen::SelfAdjointEigenSolver<gtsam::Matrix33> eigen_solver{matrix};
    if (eigen_solver.info() != Eigen::Success) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Position covariance eigendecomposition failed"};
    }

    auto eigenvalues = eigen_solver.eigenvalues();
    const double scale =
        std::max(1.0, eigenvalues.cwiseAbs().maxCoeff());
    if (eigenvalues.minCoeff() < -scale * 1e-12) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Position covariance must be positive semidefinite"};
    }

    const double min_variance = minimum_sigma * minimum_sigma;
    for (Eigen::Index index = 0; index < 3; ++index) {
        eigenvalues(index) = std::max(eigenvalues(index), min_variance);
    }
    matrix =
        eigen_solver.eigenvectors() *
        eigenvalues.asDiagonal() *
        eigen_solver.eigenvectors().transpose();

    return gtsam::noiseModel::Gaussian::Covariance(matrix);
}

[[nodiscard]] gtsam::SharedNoiseModel sourceMeasurementNoise(
    gtsam::SharedNoiseModel base,
    const GtsamSpatialFusionOptions& options) {
    if (!options.enable_huber_loss) {
        return base;
    }

    return gtsam::noiseModel::Robust::Create(
        gtsam::noiseModel::mEstimator::Huber::Create(options.huber_k),
        base);
}

[[nodiscard]] std::optional<Vec3> triangulatedBearingGuess(
    Span<const BearingObservation> bearings) {
    if (bearings.size() < 2) {
        return std::nullopt;
    }

    gtsam::Matrix33 normal = gtsam::Matrix33::Zero();
    gtsam::Vector3 rhs = gtsam::Vector3::Zero();

    for (const auto& observation : bearings) {
        const Vec3 world_direction = Direction3D::fromVector(
            rotateLocalToWorld(
                observation.sensor_pose.orientation,
                observation.bearing.direction.vector()))
                                         .vector();

        const gtsam::Vector3 direction{
            world_direction.x,
            world_direction.y,
            world_direction.z};
        const gtsam::Vector3 origin{
            observation.sensor_pose.position.x,
            observation.sensor_pose.position.y,
            observation.sensor_pose.position.z};

        const gtsam::Matrix33 projector =
            gtsam::Matrix33::Identity() - direction * direction.transpose();
        normal += projector;
        rhs += projector * origin;
    }

    Eigen::SelfAdjointEigenSolver<gtsam::Matrix33> eigen_solver{normal};
    if (eigen_solver.info() != Eigen::Success) {
        return std::nullopt;
    }

    const auto eigenvalues = eigen_solver.eigenvalues();
    const double maximum = eigenvalues.maxCoeff();
    if (!finite(maximum) || maximum <= 0.0 ||
        eigenvalues.minCoeff() <= maximum * 1e-9) {
        return std::nullopt;
    }

    const gtsam::Vector3 estimate = normal.ldlt().solve(rhs);
    if (!estimate.allFinite()) {
        return std::nullopt;
    }

    return Vec3{estimate.x(), estimate.y(), estimate.z()};
}

[[nodiscard]] bool hasFullRankRangeGeometry(
    Span<const ScalarRangeObservation> ranges) {
    if (ranges.size() < 4) {
        return false;
    }

    const gtsam::Vector3 reference{
        ranges[0].sensor_pose.position.x,
        ranges[0].sensor_pose.position.y,
        ranges[0].sensor_pose.position.z};
    gtsam::Matrix33 scatter = gtsam::Matrix33::Zero();

    for (std::size_t index = 1; index < ranges.size(); ++index) {
        const gtsam::Vector3 point{
            ranges[index].sensor_pose.position.x,
            ranges[index].sensor_pose.position.y,
            ranges[index].sensor_pose.position.z};
        const gtsam::Vector3 delta = point - reference;
        scatter += delta * delta.transpose();
    }

    Eigen::SelfAdjointEigenSolver<gtsam::Matrix33> eigen_solver{scatter};
    if (eigen_solver.info() != Eigen::Success) {
        return false;
    }

    const auto eigenvalues = eigen_solver.eigenvalues();
    const double maximum = eigenvalues.maxCoeff();
    return finite(maximum) &&
           maximum > 0.0 &&
           eigenvalues.minCoeff() > maximum * 1e-9;
}

[[nodiscard]] std::optional<gtsam::Matrix33> validatedMarginalCovariance(
    const gtsam::Matrix& covariance) {
    if (covariance.rows() != 3 || covariance.cols() != 3) {
        return std::nullopt;
    }

    gtsam::Matrix33 matrix;
    for (Eigen::Index row = 0; row < 3; ++row) {
        for (Eigen::Index column = 0; column < 3; ++column) {
            const double value = covariance(row, column);
            if (!finite(value)) {
                return std::nullopt;
            }
            matrix(row, column) = value;
        }
    }

    matrix = 0.5 * (matrix + matrix.transpose());
    Eigen::SelfAdjointEigenSolver<gtsam::Matrix33> eigen_solver{matrix};
    if (eigen_solver.info() != Eigen::Success) {
        return std::nullopt;
    }

    const auto eigenvalues = eigen_solver.eigenvalues();
    const double scale =
        std::max(1.0, eigenvalues.cwiseAbs().maxCoeff());
    if (eigenvalues.minCoeff() < -scale * 1e-10) {
        return std::nullopt;
    }

    return matrix;
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
        options.sensor_position_sigma_m,
        options.minimum_sigma,
        options.fallback_initial_range_m,
        options.huber_k};

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

    if (const auto triangulated = triangulatedBearingGuess(input.bearings);
        triangulated.has_value()) {
        return *triangulated;
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

    if (input.positions.empty() && input.ranges.empty() &&
        !triangulatedBearingGuess(input.bearings).has_value()) {
        return std::nullopt;
    }

    if (input.positions.empty() && input.bearings.empty() &&
        !hasFullRankRangeGeometry(input.ranges)) {
        return std::nullopt;
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
        const gtsam::Pose3 sensor = toPose(sensor_pose);
        target_initial.insert(sensor_key, sensor);
        target_graph.emplace_shared<gtsam::PriorFactor<gtsam::Pose3>>(
            sensor_key,
            sensor,
            gtsam::noiseModel::Isotropic::Sigma(
                6,
                clampSigma(
                    options_.sensor_position_sigma_m,
                    options_.minimum_sigma)));
        return sensor_key;
    };

    for (const auto& observation : input.bearings) {
        const gtsam::Key sensor_key =
            addSensor(observation.sensor_pose, graph, initial, sensor_index++);
        const Vec3 local_direction =
            observation.bearing.direction.vector();

        graph.emplace_shared<
            gtsam::BearingFactor<gtsam::Pose3, gtsam::Point3>>(
            sensor_key,
            source_key,
            gtsam::Unit3{gtsam::Point3{
                local_direction.x,
                local_direction.y,
                local_direction.z}},
            sourceMeasurementNoise(
                gtsam::noiseModel::Isotropic::Sigma(
                    2,
                    sigmaFromVariance(
                        observation.bearing.angular_variance_rad2,
                        options_.default_bearing_sigma_rad,
                        options_.minimum_sigma)),
                options_));
    }

    for (const auto& observation : input.ranges) {
        const gtsam::Key sensor_key =
            addSensor(observation.sensor_pose, graph, initial, sensor_index++);
        const double sigma = clampSigma(
            std::sqrt(observation.distance_m.variance),
            options_.minimum_sigma);

        graph.emplace_shared<
            gtsam::RangeFactor<gtsam::Pose3, gtsam::Point3>>(
            sensor_key,
            source_key,
            observation.distance_m.mean,
            sourceMeasurementNoise(
                gtsam::noiseModel::Isotropic::Sigma(1, sigma),
                options_));
    }

    for (const auto& observation : input.positions) {
        graph.emplace_shared<gtsam::PriorFactor<gtsam::Point3>>(
            source_key,
            toPoint(observation.estimate.mean_m),
            sourceMeasurementNoise(
                positionNoise(
                    observation.estimate.covariance_m2,
                    options_.minimum_sigma),
                options_));
    }

    try {
        gtsam::LevenbergMarquardtParams params;
        params.setMaxIterations(static_cast<int>(options_.max_iterations));
        params.setVerbosityLM("SILENT");

        const gtsam::Values result =
            gtsam::LevenbergMarquardtOptimizer{graph, initial, params}.optimize();
        const gtsam::Point3 point = result.at<gtsam::Point3>(source_key);

        const gtsam::Marginals marginals{graph, result};
        const auto covariance = validatedMarginalCovariance(
            marginals.marginalCovariance(source_key));
        if (!covariance.has_value()) {
            return std::nullopt;
        }

        PositionEstimate estimate{};
        estimate.mean_m = Vec3{point.x(), point.y(), point.z()};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                estimate.covariance_m2[row * 3 + column] = (*covariance)(
                    static_cast<Eigen::Index>(row),
                    static_cast<Eigen::Index>(column));
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
