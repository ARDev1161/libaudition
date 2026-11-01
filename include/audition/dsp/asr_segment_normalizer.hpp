#pragma once

#include <audition/core/span.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace audition::dsp {

// Optional, bounded offline ASR-only gain. This is not a VAD, noise
// suppressor, SNR improver, or dynamic AGC. Do not apply it to the PCM
// recorder: source-relative offsets must remain unchanged.
struct AsrSegmentNormalizeOptions {
    double target_rms_dbfs{-26.0};
    double max_boost_db{18.0};
    double minimum_input_rms_dbfs{-65.0};
    double peak_ceiling_dbfs{-3.0};
};

struct AsrSegmentNormalizeResult {
    double input_rms_dbfs{-180.0};
    double applied_gain_db{0.0};
    double output_rms_dbfs{-180.0};
};

// Works in-place on an already VAD-accepted segment, off the capture thread.
// Silence and almost-silence pass through unamplified; no samples are added,
// removed, clipped or time-shifted. Validates the complete segment first.
[[nodiscard]] inline AsrSegmentNormalizeResult normalizeAsrSegment(
    Span<float> pcm, AsrSegmentNormalizeOptions options = {}) {
    if (!std::isfinite(options.target_rms_dbfs) ||
        !std::isfinite(options.max_boost_db) ||
        !std::isfinite(options.minimum_input_rms_dbfs) ||
        !std::isfinite(options.peak_ceiling_dbfs) ||
        options.target_rms_dbfs > 0.0 ||
        options.target_rms_dbfs < -100.0 ||
        options.minimum_input_rms_dbfs < -120.0 ||
        options.minimum_input_rms_dbfs > options.target_rms_dbfs ||
        options.max_boost_db < 0.0 || options.max_boost_db > 60.0 ||
        options.peak_ceiling_dbfs > 0.0 ||
        options.peak_ceiling_dbfs < -60.0)
        throw std::invalid_argument{"Invalid ASR segment normalization options"};
    if (!pcm.data() && !pcm.empty())
        throw std::invalid_argument{"Invalid ASR PCM pointer"};
    AsrSegmentNormalizeResult result{};
    if (pcm.empty()) return result;
    double power = 0.0;
    double peak = 0.0;
    for (const float value : pcm) {
        if (!std::isfinite(value) || std::abs(value) > 1.0F)
            throw std::invalid_argument{"Invalid normalized ASR PCM sample"};
        const double sample = static_cast<double>(value);
        power += sample * sample;
        peak = std::max(peak, std::abs(sample));
    }
    const double rms = std::sqrt(power / static_cast<double>(pcm.size()));
    if (rms == 0.0) return result;
    result.input_rms_dbfs = 20.0 * std::log10(rms);
    result.output_rms_dbfs = result.input_rms_dbfs;
    if (result.input_rms_dbfs < options.minimum_input_rms_dbfs) return result;
    const double peak_dbfs = 20.0 * std::log10(peak);
    result.applied_gain_db = std::max(0.0,
        std::min({options.max_boost_db,
                  options.target_rms_dbfs - result.input_rms_dbfs,
                  options.peak_ceiling_dbfs - peak_dbfs}));
    if (result.applied_gain_db == 0.0) return result;
    const double factor = std::pow(10.0, result.applied_gain_db / 20.0);
    for (auto& value : pcm) {
        value = static_cast<float>(static_cast<double>(value) * factor);
    }
    result.output_rms_dbfs += result.applied_gain_db;
    return result;
}

} // namespace audition::dsp
