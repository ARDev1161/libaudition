#include <audition/spatial/spl_calibration.hpp>

#include <cmath>
#include <limits>
#include <utility>

#include <audition/core/error.hpp>

namespace audition {
namespace {

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

void validatePose(const Pose3D& pose) {
    const double values[] = {
        pose.position.x,
        pose.position.y,
        pose.position.z,
        pose.orientation.w,
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z};

    for (const double value : values) {
        if (!finite(value)) {
            throw Error{
                ErrorCode::InvalidArgument,
                "SPL calibration sensor pose must contain only finite values"};
        }
    }

    const double norm2 =
        pose.orientation.w * pose.orientation.w +
        pose.orientation.x * pose.orientation.x +
        pose.orientation.y * pose.orientation.y +
        pose.orientation.z * pose.orientation.z;
    if (!finite(norm2) || norm2 <= 1.0e-24) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration sensor orientation must be non-zero"};
    }
}

void validateProfile(
    const SoundPressureCalibrationProfile& profile) {
    if (profile.profile_id.empty()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration profile_id must not be empty"};
    }
    if (profile.signal_path_id.empty()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration signal_path_id must not be empty"};
    }

    if (!profile.reference_level_db_spl.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration reference SPL must be finite with non-negative variance"};
    }
    if (!profile.measured_level_dbfs.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration reference dBFS must be finite with non-negative variance"};
    }

    if (!finite(profile.reference_frequency_hz) ||
        profile.reference_frequency_hz <= 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration reference frequency must be finite and positive"};
    }

    if (!finite(profile.transfer_variance_db2) ||
        profile.transfer_variance_db2 < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration transfer variance must be finite and non-negative"};
    }
}

}  // namespace

SoundPressureLevelCalibrator::SoundPressureLevelCalibrator(
    SoundPressureCalibrationProfile profile)
    : profile_(std::move(profile)) {
    validateProfile(profile_);

    offset_db_ =
        profile_.reference_level_db_spl.mean -
        profile_.measured_level_dbfs.mean;
    calibration_variance_db2_ =
        profile_.reference_level_db_spl.variance +
        profile_.measured_level_dbfs.variance +
        profile_.transfer_variance_db2;

    if (!finite(offset_db_) ||
        !finite(calibration_variance_db2_) ||
        calibration_variance_db2_ < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SPL calibration profile produces invalid offset or variance"};
    }
}

std::optional<SoundLevelObservation>
SoundPressureLevelCalibrator::calibrate(
    const DbfsLevelObservation& observation) const {
    if (observation.signal_path_id.empty()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "dBFS observation signal_path_id must not be empty"};
    }
    if (observation.signal_path_id != profile_.signal_path_id) {
        throw Error{
            ErrorCode::InvalidArgument,
            "dBFS observation signal path does not match SPL calibration profile"};
    }
    if (observation.weighting != profile_.weighting) {
        throw Error{
            ErrorCode::InvalidArgument,
            "dBFS observation weighting does not match SPL calibration profile"};
    }
    if (!finite(observation.variance_db2) ||
        observation.variance_db2 < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "dBFS observation variance must be finite and non-negative"};
    }

    validatePose(observation.sensor_pose);

    if (observation.level_dbfs ==
        -std::numeric_limits<double>::infinity()) {
        return std::nullopt;
    }
    if (!finite(observation.level_dbfs)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "dBFS observation level must be finite or negative infinity for silence"};
    }

    const double mean =
        observation.level_dbfs + offset_db_;
    const double variance =
        observation.variance_db2 +
        calibration_variance_db2_;

    if (!finite(mean) ||
        !finite(variance) ||
        variance < 0.0) {
        throw Error{
            ErrorCode::ProcessingError,
            "SPL calibration produced an invalid output level"};
    }

    SoundLevelObservation result{};
    result.timestamp = observation.timestamp;
    result.level_db_spl = {mean, variance};
    result.weighting = observation.weighting;
    result.sensor_pose = observation.sensor_pose;
    return result;
}

double SoundPressureLevelCalibrator::offsetDb() const noexcept {
    return offset_db_;
}

double
SoundPressureLevelCalibrator::calibrationVarianceDb2() const noexcept {
    return calibration_variance_db2_;
}

const SoundPressureCalibrationProfile&
SoundPressureLevelCalibrator::profile() const noexcept {
    return profile_;
}

}  // namespace audition
