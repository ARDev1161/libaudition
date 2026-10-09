#include <audition/spatial/temporal_smoother.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

#include <audition/core/error.hpp>

namespace audition {
namespace {

constexpr double kCovarianceTolerance = 1.0e-10;

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

void validateOptions(const TemporalSpatialSmootherOptions& options) {
    const double weights[] = {
        options.range_measurement_weight,
        options.position_measurement_weight};

    for (const double weight : weights) {
        if (!finite(weight) || weight < 0.0 || weight > 1.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Temporal smoothing measurement weights must be finite and in [0, 1]"};
        }
    }

    const double process_variances[] = {
        options.range_process_variance_m2_per_s,
        options.position_process_variance_m2_per_s};

    for (const double variance_rate : process_variances) {
        if (!finite(variance_rate) || variance_rate < 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Temporal smoothing process variance rates must be finite and non-negative"};
        }
    }

    if (options.max_gap.nanoseconds() < 0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing max_gap must be non-negative"};
    }

    if (options.max_tracks == 0U) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing max_tracks must be non-zero"};
    }
}

void validateTrackLifecycle(const SpatialTrack& track) {
    if (!track.track_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing requires a valid SpatialTrackId"};
    }

    if (!track.first_seen.comparableWith(track.last_seen)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "SpatialTrack first_seen and last_seen must use the same clock identity"};
    }

    if (track.last_seen.nanoseconds() < track.first_seen.nanoseconds()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "SpatialTrack last_seen must not precede first_seen"};
    }
}

void validateRange(const RangeEstimate& range) {
    if (!range.distance_m.valid() ||
        range.distance_m.mean < 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing range must have finite non-negative mean and variance"};
    }
}

[[nodiscard]] double covarianceScale(
    const Covariance3& covariance) noexcept {
    double scale = 1.0;
    for (const double value : covariance) {
        if (finite(value)) {
            scale = std::max(scale, std::abs(value));
        }
    }
    return scale;
}

[[nodiscard]] Covariance3 canonicalCovariance(
    const Covariance3& input) {
    for (const double value : input) {
        if (!finite(value)) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Temporal smoothing position covariance must be finite"};
        }
    }

    Covariance3 covariance = input;
    const double scale = covarianceScale(input);

    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = row + 1; column < 3; ++column) {
            const std::size_t a = row * 3 + column;
            const std::size_t b = column * 3 + row;
            const double pair_scale = std::max(
                {1.0, std::abs(input[a]), std::abs(input[b])});
            if (std::abs(input[a] - input[b]) >
                kCovarianceTolerance * pair_scale) {
                throw Error{
                    ErrorCode::InvalidArgument,
                    "Temporal smoothing position covariance must be symmetric"};
            }

            const double average = 0.5 * (input[a] + input[b]);
            covariance[a] = average;
            covariance[b] = average;
        }
    }

    const double c00 = covariance[0];
    const double c01 = covariance[1];
    const double c02 = covariance[2];
    const double c11 = covariance[4];
    const double c12 = covariance[5];
    const double c22 = covariance[8];

    const double tol1 = kCovarianceTolerance * scale;
    if (c00 < -tol1 || c11 < -tol1 || c22 < -tol1) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing position covariance must be positive semidefinite"};
    }

    const double tol2 =
        kCovarianceTolerance * scale * scale;
    const double minor01 = c00 * c11 - c01 * c01;
    const double minor02 = c00 * c22 - c02 * c02;
    const double minor12 = c11 * c22 - c12 * c12;
    if (minor01 < -tol2 ||
        minor02 < -tol2 ||
        minor12 < -tol2) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing position covariance must be positive semidefinite"};
    }

    const double determinant =
        c00 * (c11 * c22 - c12 * c12) -
        c01 * (c01 * c22 - c12 * c02) +
        c02 * (c01 * c12 - c11 * c02);
    const double tol3 =
        kCovarianceTolerance * scale * scale * scale;
    if (determinant < -tol3) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing position covariance must be positive semidefinite"};
    }

    return covariance;
}

void validatePosition(const PositionEstimate& position) {
    if (!finite(position.mean_m.x) ||
        !finite(position.mean_m.y) ||
        !finite(position.mean_m.z)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing position mean must be finite"};
    }

    static_cast<void>(
        canonicalCovariance(position.covariance_m2));
}

[[nodiscard]] double elapsedSeconds(
    Timestamp current,
    Timestamp previous) {
    if (!current.comparableWith(previous)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "Temporal smoothing cannot combine different clock identities"};
    }

    const std::int64_t delta_ns =
        current.nanoseconds() - previous.nanoseconds();
    if (delta_ns < 0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing observations must be non-decreasing per track"};
    }

    return static_cast<double>(delta_ns) / 1'000'000'000.0;
}

[[nodiscard]] bool gapExceeds(
    Timestamp current,
    Timestamp previous,
    Duration max_gap) {
    if (!current.comparableWith(previous)) {
        throw Error{
            ErrorCode::ClockDomainMismatch,
            "Temporal smoothing cannot combine different clock identities"};
    }

    const std::int64_t delta_ns =
        current.nanoseconds() - previous.nanoseconds();
    if (delta_ns < 0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Temporal smoothing observations must be non-decreasing per track"};
    }

    return delta_ns > max_gap.nanoseconds();
}

[[nodiscard]] Gaussian1D mixGaussian1D(
    const Gaussian1D& previous,
    const Gaussian1D& measurement,
    double measurement_weight,
    double process_variance) {
    const double previous_weight = 1.0 - measurement_weight;

    const double mean =
        previous_weight * previous.mean +
        measurement_weight * measurement.mean;
    const double delta = previous.mean - measurement.mean;
    const double variance =
        previous_weight * (previous.variance + process_variance) +
        measurement_weight * measurement.variance +
        previous_weight * measurement_weight * delta * delta;

    if (!finite(mean) ||
        !finite(variance) ||
        mean < 0.0 ||
        variance < 0.0) {
        throw Error{
            ErrorCode::ProcessingError,
            "Temporal range smoothing produced invalid moments"};
    }

    return {mean, variance};
}

[[nodiscard]] Vec3 mixMean(
    const Vec3& previous,
    const Vec3& measurement,
    double measurement_weight) {
    const double previous_weight = 1.0 - measurement_weight;
    const Vec3 result{
        previous_weight * previous.x +
            measurement_weight * measurement.x,
        previous_weight * previous.y +
            measurement_weight * measurement.y,
        previous_weight * previous.z +
            measurement_weight * measurement.z};

    if (!finite(result.x) ||
        !finite(result.y) ||
        !finite(result.z)) {
        throw Error{
            ErrorCode::ProcessingError,
            "Temporal position smoothing produced invalid mean"};
    }

    return result;
}

[[nodiscard]] Covariance3 mixCovariance(
    const Vec3& previous_mean,
    const Covariance3& previous_covariance,
    const Vec3& measurement_mean,
    const Covariance3& measurement_covariance,
    double measurement_weight,
    double process_variance) {
    const double previous_weight = 1.0 - measurement_weight;

    Covariance3 predicted = previous_covariance;
    predicted[0] += process_variance;
    predicted[4] += process_variance;
    predicted[8] += process_variance;

    for (const double value : predicted) {
        if (!finite(value)) {
            throw Error{
                ErrorCode::ProcessingError,
                "Temporal position process variance overflowed"};
        }
    }

    const Vec3 delta{
        previous_mean.x - measurement_mean.x,
        previous_mean.y - measurement_mean.y,
        previous_mean.z - measurement_mean.z};
    const double components[] = {delta.x, delta.y, delta.z};

    Covariance3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            const std::size_t index = row * 3 + column;
            result[index] =
                previous_weight * predicted[index] +
                measurement_weight * measurement_covariance[index] +
                previous_weight * measurement_weight *
                    components[row] * components[column];

            if (!finite(result[index])) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "Temporal position smoothing produced invalid covariance"};
            }
        }
    }

    return canonicalCovariance(result);
}

}  // namespace

TemporalSpatialTrackSmoother::TemporalSpatialTrackSmoother(
    TemporalSpatialSmootherOptions options)
    : options_(options) {
    validateOptions(options_);
}

void TemporalSpatialTrackSmoother::update(
    SpatialTrack& track) {
    validateTrackLifecycle(track);

    if (track.range.has_value()) {
        validateRange(*track.range);
    }
    if (track.position.has_value()) {
        validatePosition(*track.position);
    }

    const auto existing = states_.find(track.track_id);

    if (existing != states_.end() &&
        existing->second.last_observation.has_value()) {
        static_cast<void>(
            elapsedSeconds(
                track.last_seen,
                *existing->second.last_observation));
    }

    if (!track.range.has_value() &&
        !track.position.has_value()) {
        if (existing != states_.end()) {
            TrackState next_state = existing->second;
            next_state.last_observation = track.last_seen;
            states_[track.track_id] = std::move(next_state);
        }
        return;
    }

    if (existing == states_.end() &&
        states_.size() >= options_.max_tracks) {
        throw Error{
            ErrorCode::InvalidState,
            "Temporal spatial smoother track capacity exceeded"};
    }

    TrackState next_state =
        existing == states_.end()
            ? TrackState{}
            : existing->second;
    next_state.last_observation = track.last_seen;
    SpatialTrack candidate = track;

    if (track.range.has_value()) {
        const RangeEstimate measurement = *track.range;

        if (!next_state.range.has_value() ||
            gapExceeds(
                track.last_seen,
                next_state.range->timestamp,
                options_.max_gap)) {
            next_state.range =
                RangeState{measurement.distance_m, track.last_seen};
        } else {
            const double dt =
                elapsedSeconds(
                    track.last_seen,
                    next_state.range->timestamp);
            const double process_variance =
                options_.range_process_variance_m2_per_s * dt;
            if (!finite(process_variance)) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "Temporal range process variance overflowed"};
            }

            if (options_.range_measurement_weight == 1.0) {
                next_state.range =
                    RangeState{measurement.distance_m, track.last_seen};
            } else {
                const Gaussian1D smoothed =
                    mixGaussian1D(
                        next_state.range->estimate,
                        measurement.distance_m,
                        options_.range_measurement_weight,
                        process_variance);

                candidate.range = RangeEstimate{
                    smoothed,
                    Probability::zero(),
                    RangeEstimate::Method::Fused};
                next_state.range =
                    RangeState{smoothed, track.last_seen};
            }
        }
    }

    if (track.position.has_value()) {
        const PositionEstimate measurement = *track.position;
        const Covariance3 measurement_covariance =
            canonicalCovariance(
                measurement.covariance_m2);

        if (!next_state.position.has_value() ||
            gapExceeds(
                track.last_seen,
                next_state.position->timestamp,
                options_.max_gap)) {
            next_state.position =
                PositionState{
                    measurement.mean_m,
                    measurement_covariance,
                    track.last_seen};
        } else {
            const double dt =
                elapsedSeconds(
                    track.last_seen,
                    next_state.position->timestamp);
            const double process_variance =
                options_.position_process_variance_m2_per_s * dt;
            if (!finite(process_variance)) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "Temporal position process variance overflowed"};
            }

            if (options_.position_measurement_weight == 1.0) {
                next_state.position =
                    PositionState{
                        measurement.mean_m,
                        measurement_covariance,
                        track.last_seen};
            } else {
                const Vec3 smoothed_mean =
                    mixMean(
                        next_state.position->mean,
                        measurement.mean_m,
                        options_.position_measurement_weight);
                const Covariance3 smoothed_covariance =
                    mixCovariance(
                        next_state.position->mean,
                        next_state.position->covariance,
                        measurement.mean_m,
                        measurement_covariance,
                        options_.position_measurement_weight,
                        process_variance);

                PositionEstimate smoothed{};
                smoothed.mean_m = smoothed_mean;
                smoothed.covariance_m2 = smoothed_covariance;
                smoothed.confidence = Probability::zero();

                candidate.position = smoothed;
                next_state.position =
                    PositionState{
                        smoothed_mean,
                        smoothed_covariance,
                        track.last_seen};
            }
        }
    }

    const SpatialTrackId track_id = track.track_id;
    track = std::move(candidate);
    states_[track_id] = std::move(next_state);
}

void TemporalSpatialTrackSmoother::endTrack(
    SpatialTrackId track_id) noexcept {
    states_.erase(track_id);
}

void TemporalSpatialTrackSmoother::reset() noexcept {
    states_.clear();
}

std::size_t
TemporalSpatialTrackSmoother::trackedCount() const noexcept {
    return states_.size();
}

const TemporalSpatialSmootherOptions&
TemporalSpatialTrackSmoother::options() const noexcept {
    return options_;
}

}  // namespace audition
