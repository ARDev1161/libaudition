#pragma once

#include <optional>

#include <audition/core/export.hpp>
#include <audition/interfaces/spatial.hpp>

namespace audition {

/**
 * @brief Parameters for calibrated sound-level distance priors.
 *
 * The propagation model is:
 *
 *   L(r) = L(r0) - 10 * n * log10(r / r0)
 *
 * where n=2 is free-field spherical spreading. propagation_variance_db2 adds
 * explicit environment/model uncertainty (reverberation, occlusion, source
 * directivity, etc.) in dB^2. No hidden environment variance is injected.
 */
struct SoundLevelRangePriorOptions {
    double path_loss_exponent{2.0};
    double propagation_variance_db2{0.0};
};

/**
 * @brief Range prior from calibrated SPL and source-type acoustic-level priors.
 *
 * Source-level and measurement uncertainty are Gaussian in dB. Under the
 * log-distance propagation model, each source hypothesis induces a log-normal
 * distance distribution. Multiple hypotheses are combined by exact first and
 * second moments using their normalized non-negative relative weights.
 *
 * This estimator deliberately does not consume raw dBFS and does not fabricate
 * calibrated confidence. Returned confidence is Probability::zero().
 */
class AUDITION_API SoundLevelRangePriorEstimator final
    : public IRangeEstimator {
public:
    explicit SoundLevelRangePriorEstimator(
        SoundLevelRangePriorOptions options = {});

    [[nodiscard]] BackendInfo backendInfo() const override;

    [[nodiscard]] std::optional<RangeEstimate> estimate(
        const RangeEstimationInput& input) const override;

    /**
     * @brief Convenience conversion to a pose-anchored scalar range observation.
     *
     * Uses the timestamp and sensor pose from input.sound_level.
     */
    [[nodiscard]] std::optional<ScalarRangeObservation>
    estimateObservation(const RangeEstimationInput& input) const;

    [[nodiscard]] const SoundLevelRangePriorOptions& options() const noexcept;

private:
    SoundLevelRangePriorOptions options_{};
};

}  // namespace audition
