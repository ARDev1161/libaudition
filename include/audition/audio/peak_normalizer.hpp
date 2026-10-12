#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace audition {

// Offline peak normalization for diagnostic playback, independent of Qt/ODAS.
// Never modifies the input; no compression/AGC is applied.
struct PeakNormalizeOptions {
    double target_peak_dbfs{-3.0};
    double max_gain_db{40.0};
    bool allow_attenuation{false};
};
struct PeakNormalizeResult {
    double input_peak_dbfs{-180.0};
    double applied_gain_db{0.0};
    double output_peak_dbfs{-180.0};
    std::vector<std::int16_t> pcm;
};

inline PeakNormalizeResult peakNormalizePcm16(
    const std::vector<std::int16_t>& input, PeakNormalizeOptions options = {}) {
    if (!std::isfinite(options.target_peak_dbfs) ||
        !std::isfinite(options.max_gain_db) || options.target_peak_dbfs > 0 ||
        options.max_gain_db < 0 || options.max_gain_db > 120)
        throw std::invalid_argument("Invalid peak normalization options");
    PeakNormalizeResult result;
    result.pcm.reserve(input.size());
    std::uint32_t peak = 0;
    for (const auto value : input)
        peak = std::max(peak, static_cast<std::uint32_t>(
            std::abs(static_cast<int>(value))));
    if (peak == 0) {
        result.pcm = input;
        return result;
    }
    result.input_peak_dbfs = 20.0 * std::log10(static_cast<double>(peak) / 32768.0);
    const double requested = options.target_peak_dbfs - result.input_peak_dbfs;
    result.applied_gain_db = std::clamp(requested,
        options.allow_attenuation ? -120.0 : 0.0, options.max_gain_db);
    const double factor = std::pow(10.0, result.applied_gain_db / 20.0);
    std::uint32_t outputPeak = 0;
    for (const auto value : input) {
        const auto scaled = static_cast<int>(std::lrint(value * factor));
        const auto safe = static_cast<std::int16_t>(std::clamp(scaled, -32768, 32767));
        result.pcm.push_back(safe);
        outputPeak = std::max(outputPeak, static_cast<std::uint32_t>(
            std::abs(static_cast<int>(safe))));
    }
    result.output_peak_dbfs = 20.0 * std::log10(
        static_cast<double>(outputPeak) / 32768.0);
    return result;
}
} // namespace audition
