#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace demo {

// Experimental live-capture energy gate. It never modifies PCM: when a
// majority of mapped microphone channels contain no acoustic activity above
// the learned noise floor, the caller can skip ODAS and release stale SST IDs.
// The first second is calibration and must be quiet. Speech-only VAD/ASR still
// operates on unchanged ODAS source audio when the gate opens.
class AdaptiveArrayPresenceGate final {
public:
    struct Status {
        bool process_audio{true};
        bool became_inactive{false};
        bool calibrating{true};
        double level_dbfs{-180.0};
        double noise_dbfs{-180.0};
        double above_noise_db{0.0};
    };

    explicit AdaptiveArrayPresenceGate(std::uint32_t sample_rate_hz,
                                       double onset_margin_db = 4.0)
        : sample_rate_hz_(sample_rate_hz), onset_margin_db_(onset_margin_db) {
        if (sample_rate_hz_ == 0U || !std::isfinite(onset_margin_db) ||
            onset_margin_db < 1.0 || onset_margin_db > 20.0)
            throw std::invalid_argument("Invalid array gate options");
    }

    [[nodiscard]] Status update(const float* interleaved,
                                std::size_t frame_count,
                                std::size_t channel_count,
                                const std::vector<std::size_t>& microphone_channels) {
        if (!interleaved || frame_count == 0U || channel_count == 0U ||
            microphone_channels.empty())
            throw std::invalid_argument("Invalid array gate PCM");
        std::vector<double> levels;
        levels.reserve(microphone_channels.size());
        for (const auto channel : microphone_channels) {
            if (channel >= channel_count)
                throw std::invalid_argument("Array gate microphone index invalid");
            double power = 0.0;
            for (std::size_t i = 0; i < frame_count; ++i) {
                const double sample = interleaved[i * channel_count + channel];
                if (!std::isfinite(sample))
                    throw std::invalid_argument("Nonfinite array gate sample");
                power += sample * sample;
            }
            const double rms = std::sqrt(power / static_cast<double>(frame_count));
            levels.push_back(20.0 * std::log10(std::max(1.0e-9, rms)));
        }
        // Median across the physical microphone channels; AUX/reference
        // channels have no influence, nor does a single noisy microphone.
        std::sort(levels.begin(), levels.end());
        const auto n = levels.size();
        const double level = n % 2U == 0U
            ? (levels[n / 2U - 1U] + levels[n / 2U]) * 0.5
            : levels[n / 2U];
        if (!initialized_) {
            initialized_ = true;
            noise_dbfs_ = level;
        }
        const double seconds = static_cast<double>(frame_count) /
                               static_cast<double>(sample_rate_hz_);
        if (calibration_seconds_ < 1.0) {
            const double alpha = -std::expm1(-seconds / 0.15);
            noise_dbfs_ += alpha * (level - noise_dbfs_);
            calibration_seconds_ += seconds;
            active_ = true;
            return {true, false, true, level, noise_dbfs_, level - noise_dbfs_};
        }
        const double relative_level = level - noise_dbfs_;
        const bool significant = active_
            ? relative_level >= std::max(1.0, onset_margin_db_ - 2.0)
            : relative_level >= onset_margin_db_;
        if (significant) {
            active_ = true;
            silence_seconds_ = 0.0;
        } else if (active_) {
            silence_seconds_ += seconds;
            if (silence_seconds_ >= 1.5) {
                active_ = false;
                silence_seconds_ = 0.0;
            }
        } else {
            // Follow slow ambient noise changes only while inactive;
            // loud transients still trigger against the old floor.
            const double tau = level < noise_dbfs_ ? 1.0 : 15.0;
            const double alpha = -std::expm1(-seconds / tau);
            noise_dbfs_ += alpha * (level - noise_dbfs_);
        }
        const bool ended = was_active_ && !active_;
        was_active_ = active_;
        return {active_, ended, false, level, noise_dbfs_,
                level - noise_dbfs_};
    }

    void reset() noexcept {
        initialized_ = false;
        calibration_seconds_ = 0.0;
        silence_seconds_ = 0.0;
        active_ = true;
        was_active_ = true;
        noise_dbfs_ = -180.0;
    }

private:
    std::uint32_t sample_rate_hz_{};
    double onset_margin_db_{};
    bool initialized_{false};
    bool active_{true};
    bool was_active_{true};
    double noise_dbfs_{-180.0};
    double calibration_seconds_{0.0};
    double silence_seconds_{0.0};
};

} // namespace demo
