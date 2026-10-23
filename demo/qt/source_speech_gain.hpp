#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace demo {

// Post-ODAS gain for speech inference only; never change localization,
// classification or archived source WAV PCM. Instantiate per ODAS track.
class SourceSpeechGain {
public:
    struct Metrics {
        double input_rms_dbfs{-180.0};
        double output_rms_dbfs{-180.0};
        double applied_gain_db{0.0};
    };

    [[nodiscard]] std::vector<float> process(
        const float* samples, std::size_t count, std::uint32_t sampleRate) {
        if (!sampleRate || (count != 0U && !samples))
            throw std::invalid_argument("Invalid source speech gain input");
        std::vector<float> output(count, 0.0F);
        if (count == 0U) return output;

        double energy = 0.0;
        double peak = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double sample = samples[i];
            if (!std::isfinite(sample))
                throw std::invalid_argument("Nonfinite source speech PCM");
            energy += sample * sample;
            peak = std::max(peak, std::abs(sample));
        }
        const double rms = std::sqrt(energy / static_cast<double>(count));
        metrics_.input_rms_dbfs = dbfs(rms);

        constexpr double minActiveRms = 0.001; // -60 dBFS
        constexpr double targetRmsDbfs = -26.0;
        constexpr double maxGainDb = 24.0;
        constexpr double peakCeiling = 0.89;
        double wanted = rms >= minActiveRms
            ? std::clamp(targetRmsDbfs - dbfs(rms), 0.0, maxGainDb)
            : 0.0;
        // Ensure a loud onset cannot clip while prior gain is releasing.
        const double peakLimitDb = peak > 0.0
            ? 20.0 * std::log10(peakCeiling / peak) : maxGainDb;
        wanted = std::min(wanted, peakLimitDb);

        const double seconds =
            static_cast<double>(count) / static_cast<double>(sampleRate);
        const double timeConstant = wanted < gain_db_ ? 0.02 : 0.075;
        const double alpha = -std::expm1(-seconds / timeConstant);
        const double initial = std::min(gain_db_, peakLimitDb);
        const double finalDb = std::min(
            gain_db_ + alpha * (wanted - gain_db_), peakLimitDb);
        gain_db_ = finalDb;
        double outEnergy = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double fraction =
                static_cast<double>(i + 1U) / static_cast<double>(count);
            const double gain = std::pow(
                10.0, (initial + (finalDb - initial) * fraction) / 20.0);
            const double scaled = std::clamp(
                static_cast<double>(samples[i]) * gain,
                -peakCeiling, peakCeiling);
            output[i] = static_cast<float>(scaled);
            outEnergy += scaled * scaled;
        }
        metrics_.output_rms_dbfs =
            dbfs(std::sqrt(outEnergy / static_cast<double>(count)));
        metrics_.applied_gain_db = finalDb;
        return output;
    }

    [[nodiscard]] const Metrics& metrics() const noexcept { return metrics_; }
    void reset() noexcept {
        gain_db_ = 0.0;
        metrics_ = {};
    }

private:
    [[nodiscard]] static double dbfs(double value) noexcept {
        return value > 0.0 ? 20.0 * std::log10(value) : -180.0;
    }
    double gain_db_{0.0};
    Metrics metrics_{};
};

}  // namespace demo
