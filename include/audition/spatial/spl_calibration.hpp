#pragma once

#include <optional>
#include <string>

#include <audition/core/export.hpp>
#include <audition/interfaces/spatial.hpp>

namespace audition {

/**
 * @brief One dBFS level measured on a named digital signal path.
 *
 * level_dbfs may be negative infinity to represent exactly zero RMS. In that
 * case calibration returns std::nullopt because no finite SPL observation can
 * be formed. variance_db2 must always be finite and non-negative.
 */
struct DbfsLevelObservation {
    Timestamp timestamp{};
    double level_dbfs{0.0};
    double variance_db2{0.0};
    SoundLevelWeighting weighting{SoundLevelWeighting::Z};
    Pose3D sensor_pose{};
    std::string signal_path_id{};
};

/**
 * @brief Auditable reference used to derive dB SPL from dBFS.
 *
 * The profile is valid only for the same named digital signal path and
 * frequency weighting used during calibration.
 *
 * The calibrated offset is:
 *
 *   offset_db = reference_level_db_spl.mean - measured_level_dbfs.mean
 *
 * and its variance is the sum of both reference variances plus
 * transfer_variance_db2.
 */
struct SoundPressureCalibrationProfile {
    std::string profile_id{};
    std::string signal_path_id{};

    Gaussian1D reference_level_db_spl{};
    Gaussian1D measured_level_dbfs{};

    double reference_frequency_hz{1000.0};
    SoundLevelWeighting weighting{SoundLevelWeighting::Z};

    /**
     * Extra repeatability/transfer uncertainty not already represented by the
     * two reference measurements (gain drift, fixture repeatability, etc.).
     */
    double transfer_variance_db2{0.0};
};

/**
 * @brief Converts measured dBFS into calibrated dB SPL.
 *
 * This is intentionally a linear dB offset calibration. It does not infer
 * microphone sensitivity, apply A/C weighting filters, or compensate arbitrary
 * frequency response. Any weighting/filtering must have been applied on the
 * same signal path represented by the profile.
 */
class AUDITION_API SoundPressureLevelCalibrator {
public:
    explicit SoundPressureLevelCalibrator(
        SoundPressureCalibrationProfile profile);

    [[nodiscard]] std::optional<SoundLevelObservation> calibrate(
        const DbfsLevelObservation& observation) const;

    [[nodiscard]] double offsetDb() const noexcept;
    [[nodiscard]] double calibrationVarianceDb2() const noexcept;

    [[nodiscard]] const SoundPressureCalibrationProfile&
    profile() const noexcept;

private:
    SoundPressureCalibrationProfile profile_{};
    double offset_db_{0.0};
    double calibration_variance_db2_{0.0};
};

}  // namespace audition
