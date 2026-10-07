#pragma once

#include <optional>
#include <vector>

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/backend/capabilities.hpp>
#include <acoustic/core/geometry.hpp>
#include <acoustic/core/span.hpp>
#include <acoustic/spatial/types.hpp>

namespace acoustic {

class ISpatialAudioEngine {
public:
    virtual ~ISpatialAudioEngine() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual SpatialCapabilities capabilities() const = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual SpatialProcessingResult process(AudioView multichannel_audio) = 0;
};

struct BearingObservation {
    Timestamp timestamp{};
    Pose3D sensor_pose{};
    DirectionEstimate bearing{};
};

struct ScalarRangeObservation {
    Timestamp timestamp{};
    Gaussian1D distance_m{};
    Probability confidence{Probability::zero()};
    RangeEstimate::Method method{RangeEstimate::Method::Unknown};
};

struct RangeEstimationInput {
    Span<const BearingObservation> bearings{};
    Span<const ScalarRangeObservation> scalar_ranges{};
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

}  // namespace acoustic
