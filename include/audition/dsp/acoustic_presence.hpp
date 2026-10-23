#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace audition::dsp {

// Acoustic activity is evidence, NOT permission to retire an SST track.
// Implementations must not modify ODAS identity, lifecycle, or source PCM.
struct AcousticPresenceObservation {
    double rms_dbfs{-180.0};
    bool sound_present{false};
    bool calibrated{false};
};

class IAcousticPresenceDetector {
public:
    virtual ~IAcousticPresenceDetector() = default;
    virtual AcousticPresenceObservation process(const float* samples,
        std::size_t count, std::uint32_t sample_rate) = 0;
    virtual void reset() noexcept = 0;
};

// Fixed-threshold diagnostic baseline, intentionally not an adaptive noise
// model: prevents learning active speech as the background at startup.
// No silence timer, SST feedback, or source-retirement side effects.
class RmsAcousticPresenceDetector final : public IAcousticPresenceDetector {
public:
    explicit RmsAcousticPresenceDetector(double threshold_dbfs = -55.0)
        : threshold_dbfs_(threshold_dbfs) {
        if (!std::isfinite(threshold_dbfs_) ||
            threshold_dbfs_ < -180.0 || threshold_dbfs_ > 0.0)
            throw std::invalid_argument("Invalid RMS presence threshold");
    }

    AcousticPresenceObservation process(const float* samples,
        std::size_t count, std::uint32_t sample_rate) override {
        if (!sample_rate || (count && !samples))
            throw std::invalid_argument("Invalid presence PCM");
        if (!count) return {};
        double power = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(samples[i]))
                throw std::invalid_argument("Nonfinite presence sample");
            power += static_cast<double>(samples[i]) * samples[i];
        }
        const double rms = std::sqrt(power / static_cast<double>(count));
        const double dbfs = rms > 0.0 ? 20.0 * std::log10(rms) : -180.0;
        return {dbfs, dbfs >= threshold_dbfs_, false};
    }
    void reset() noexcept override {}
private:
    double threshold_dbfs_;
};
}  // namespace audition::dsp
