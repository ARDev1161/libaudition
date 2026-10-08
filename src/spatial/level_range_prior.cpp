#include <audition/spatial/level_range_prior.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <audition/core/error.hpp>

namespace audition {
namespace {

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

void validateOptions(const SoundLevelRangePriorOptions& options) {
    if (!finite(options.path_loss_exponent) ||
        options.path_loss_exponent <= 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Sound-level path_loss_exponent must be finite and positive"};
    }

    if (!finite(options.propagation_variance_db2) ||
        options.propagation_variance_db2 < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Sound-level propagation variance must be finite and non-negative"};
    }
}

void validateLevel(const SoundLevelObservation& observation) {
    if (!observation.level_db_spl.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Measured dB SPL must have finite mean and non-negative finite variance"};
    }

    if (!finite(observation.sensor_pose.position.x) ||
        !finite(observation.sensor_pose.position.y) ||
        !finite(observation.sensor_pose.position.z)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Sound-level sensor position must contain only finite coordinates"};
    }
}

void validatePrior(
    const SourceLevelPrior& prior,
    SoundLevelWeighting measurement_weighting) {
    if (prior.source_type.empty()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source-level prior requires a non-empty source_type"};
    }

    if (!prior.level_db_spl_at_reference.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source-level prior dB SPL must have finite mean and non-negative finite variance"};
    }

    if (!finite(prior.reference_distance_m) ||
        prior.reference_distance_m <= 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source-level reference distance must be finite and positive"};
    }

    if (!finite(prior.weight) || prior.weight < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source-level prior weight must be finite and non-negative"};
    }

    if (prior.weighting != measurement_weighting) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Sound-level weighting must match between measurement and source prior"};
    }
}

struct DistanceMoments {
    double mean{0.0};
    double second_moment{0.0};
};

[[nodiscard]] DistanceMoments hypothesisMoments(
    const SoundLevelObservation& observation,
    const SourceLevelPrior& prior,
    const SoundLevelRangePriorOptions& options) {
    constexpr double kLn10 = 2.30258509299404568402;
    const double scale =
        kLn10 / (10.0 * options.path_loss_exponent);

    const double level_delta_db =
        prior.level_db_spl_at_reference.mean -
        observation.level_db_spl.mean;

    const double level_variance_db2 =
        prior.level_db_spl_at_reference.variance +
        observation.level_db_spl.variance +
        options.propagation_variance_db2;

    const double log_mean =
        std::log(prior.reference_distance_m) +
        scale * level_delta_db;
    const double log_variance =
        scale * scale * level_variance_db2;

    const double mean =
        std::exp(log_mean + 0.5 * log_variance);
    const double second_moment =
        std::exp(2.0 * log_mean + 2.0 * log_variance);

    if (!finite(mean) ||
        !finite(second_moment) ||
        mean < 0.0 ||
        second_moment < 0.0) {
        throw Error{
            ErrorCode::ProcessingError,
            "Sound-level range prior exceeds representable numeric range"};
    }

    return {mean, second_moment};
}

}  // namespace

SoundLevelRangePriorEstimator::SoundLevelRangePriorEstimator(
    SoundLevelRangePriorOptions options)
    : options_(options) {
    validateOptions(options_);
}

BackendInfo SoundLevelRangePriorEstimator::backendInfo() const {
    return {"sound-level-prior", "1"};
}

std::optional<RangeEstimate>
SoundLevelRangePriorEstimator::estimate(
    const RangeEstimationInput& input) const {
    if (!input.sound_level.has_value() ||
        input.source_level_priors.empty()) {
        return std::nullopt;
    }

    const SoundLevelObservation& observation = *input.sound_level;
    validateLevel(observation);

    double total_weight = 0.0;
    double weighted_mean = 0.0;
    double weighted_second_moment = 0.0;

    for (const auto& prior : input.source_level_priors) {
        validatePrior(prior, observation.weighting);
        if (prior.weight == 0.0) {
            continue;
        }

        const DistanceMoments moments =
            hypothesisMoments(observation, prior, options_);

        total_weight += prior.weight;
        weighted_mean += prior.weight * moments.mean;
        weighted_second_moment +=
            prior.weight * moments.second_moment;

        if (!finite(total_weight) ||
            !finite(weighted_mean) ||
            !finite(weighted_second_moment)) {
            throw Error{
                ErrorCode::ProcessingError,
                "Sound-level prior mixture exceeds representable numeric range"};
        }
    }

    if (total_weight <= 0.0) {
        return std::nullopt;
    }

    const double mean = weighted_mean / total_weight;
    const double second_moment =
        weighted_second_moment / total_weight;
    double variance = second_moment - mean * mean;

    const double tolerance =
        std::numeric_limits<double>::epsilon() *
        std::max(1.0, second_moment) * 16.0;
    if (variance < 0.0 && variance >= -tolerance) {
        variance = 0.0;
    }

    if (!finite(mean) ||
        !finite(variance) ||
        mean < 0.0 ||
        variance < 0.0) {
        throw Error{
            ErrorCode::ProcessingError,
            "Sound-level range prior produced invalid moments"};
    }

    RangeEstimate result{};
    result.distance_m = {mean, variance};
    result.confidence = Probability::zero();
    result.method = RangeEstimate::Method::LevelPrior;
    return result;
}

std::optional<ScalarRangeObservation>
SoundLevelRangePriorEstimator::estimateObservation(
    const RangeEstimationInput& input) const {
    const auto range = estimate(input);
    if (!range.has_value()) {
        return std::nullopt;
    }

    const SoundLevelObservation& level = *input.sound_level;
    ScalarRangeObservation observation{};
    observation.timestamp = level.timestamp;
    observation.distance_m = range->distance_m;
    observation.confidence = range->confidence;
    observation.method = range->method;
    observation.sensor_pose = level.sensor_pose;
    return observation;
}

const SoundLevelRangePriorOptions&
SoundLevelRangePriorEstimator::options() const noexcept {
    return options_;
}

}  // namespace audition
