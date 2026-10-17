#pragma once
#include <audition/core/time.hpp>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace audition::temporal {

// Geometric compatibility of two externally localized observations.
// This does not assign AcousticSourceId or claim physical source identity.
struct SpatialGateObservation {
    Timestamp time{};
    double x{0}, y{0}, z{0}; // metres, in the SAME externally established frame
};

struct SpatialGateOptions {
    Duration max_elapsed{0}; // zero permits only simultaneous samples
    double max_distance_m{1.0};
};

struct SpatialGateResult {
    bool compatible{false};
    double distance_m{0};
    std::uint64_t elapsed_nanoseconds{0};
};

class SpatialTemporalGate final {
public:
    explicit SpatialTemporalGate(SpatialGateOptions options = {}) : options_(options) {
        if (options_.max_elapsed.nanoseconds() < 0 ||
            !std::isfinite(options_.max_distance_m) || options_.max_distance_m < 0)
            throw std::invalid_argument("invalid spatial gating options");
    }

    [[nodiscard]] SpatialGateResult evaluate(const SpatialGateObservation& lhs,
                                             const SpatialGateObservation& rhs) const {
        const auto finite = [](const SpatialGateObservation& p) {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        };
        if (!finite(lhs) || !finite(rhs) || !lhs.time.comparableWith(rhs.time))
            throw std::invalid_argument("invalid spatial observation or clock");
        // Avoid signed overflow even for endpoints of the int64 timestamp range.
        const auto a = static_cast<std::uint64_t>(lhs.time.nanoseconds());
        const auto b = static_cast<std::uint64_t>(rhs.time.nanoseconds());
        const auto elapsed = a >= b ? a - b : b - a;
        // Use hypot to avoid overflow of the individual squared components.
        const double distance = std::hypot(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
        if (!std::isfinite(distance))
            throw std::overflow_error("spatial gate distance overflow");
        // Elapsed nanoseconds can exceed Duration's signed range; an observation
        // that far apart cannot pass a nonnegative int64 max_elapsed.
        const auto max = static_cast<std::uint64_t>(options_.max_elapsed.nanoseconds());
        if (elapsed > max)
            return {false, distance, elapsed};
        return {distance <= options_.max_distance_m, distance, elapsed};
    }
private:
    SpatialGateOptions options_{};
};
} // namespace audition::temporal
