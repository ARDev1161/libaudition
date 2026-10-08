#pragma once

#include <optional>
#include <string>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/backend/capabilities.hpp>
#include <audition/core/geometry.hpp>
#include <audition/core/span.hpp>
#include <audition/spatial/types.hpp>

namespace audition {

class ISpatialAudioEngine {
public:
    virtual ~ISpatialAudioEngine() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual SpatialCapabilities capabilities() const = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual SpatialProcessingResult process(AudioView multichannel_audio) = 0;
};

/**
 * @brief Direction measured in the local sensor frame.
 *
 * sensor_pose places that local frame in the canonical/world frame.
 */
struct BearingObservation {
    Timestamp timestamp{};
    Pose3D sensor_pose{};
    DirectionEstimate bearing{};
};

/**
 * @brief Scalar source range measured from a sensor pose.
 *
 * sensor_pose is intentionally the last field to preserve source compatibility
 * with the original aggregate field order.
 */
struct ScalarRangeObservation {
    Timestamp timestamp{};
    Gaussian1D distance_m{};
    Probability confidence{Probability::zero()};
    RangeEstimate::Method method{RangeEstimate::Method::Unknown};
    Pose3D sensor_pose{};
};

enum class SoundLevelWeighting {
    Z,
    A,
    C,
};

/**
 * @brief Calibrated sound-pressure level measured at one sensor pose.
 *
 * level_db_spl is expressed in dB SPL, never dBFS. Its variance is in dB^2.
 * The weighting must match the source-level prior used for inference.
 */
struct SoundLevelObservation {
    Timestamp timestamp{};
    Gaussian1D level_db_spl{};
    SoundLevelWeighting weighting{SoundLevelWeighting::Z};
    Pose3D sensor_pose{};
};

/**
 * @brief Prior acoustic level for one source-type hypothesis.
 *
 * level_db_spl_at_reference is the expected calibrated sound-pressure level at
 * reference_distance_m. weight is a non-negative relative mixture weight; it is
 * normalized across the supplied hypotheses and is not itself exposed as a
 * calibrated confidence.
 */
struct SourceLevelPrior {
    std::string source_type{};
    Gaussian1D level_db_spl_at_reference{};
    double reference_distance_m{1.0};
    double weight{1.0};
    SoundLevelWeighting weighting{SoundLevelWeighting::Z};
};

struct RangeEstimationInput {
    Span<const BearingObservation> bearings{};
    Span<const ScalarRangeObservation> scalar_ranges{};
    std::optional<SoundLevelObservation> sound_level{};
    Span<const SourceLevelPrior> source_level_priors{};
};

class IRangeEstimator {
public:
    virtual ~IRangeEstimator() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual std::optional<RangeEstimate> estimate(
        const RangeEstimationInput& input) const = 0;
};

struct PositionObservation {
    Timestamp timestamp{};
    PositionEstimate estimate{};
};

/**
 * @brief Acoustic observations belonging to one acoustic-source hypothesis.
 *
 * All timestamps must be comparable (same ClockIdentity) before fusion.
 * This contract is intentionally acoustic-only. Cross-modal fusion with camera,
 * radar, robot state, or other non-audio sensors belongs in the application
 * consuming libaudition.
 */
struct SpatialFusionInput {
    Span<const BearingObservation> bearings{};
    Span<const ScalarRangeObservation> ranges{};
    Span<const PositionObservation> positions{};
};

class ISpatialFusion {
public:
    virtual ~ISpatialFusion() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual std::optional<PositionEstimate> fuse(
        const SpatialFusionInput& input) const = 0;
};

}  // namespace audition
