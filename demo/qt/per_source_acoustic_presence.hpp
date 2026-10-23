#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>

namespace demo {

// An independent SSS-energy release gate. ODAS SST activity can remain high
// after a speaker stops; a track is not evidence of acoustic presence by itself.
// Decisions are hysteretic and use audio time (not UI paint/timer events).
class PerSourceAcousticPresence final {
public:
    bool update(std::uint64_t id, const float* samples, std::size_t count,
                std::uint32_t sample_rate) {
        if (!samples || !count || !sample_rate) return true;
        double power = 0.0;
        for (std::size_t i = 0; i < count; ++i)
            power += static_cast<double>(samples[i]) * samples[i];
        const double rms = 20.0 * std::log10(
            std::max(1e-9, std::sqrt(power / static_cast<double>(count))));
        if (!std::isfinite(rms)) return true;
        auto& s = states_[id];
        const double duration = static_cast<double>(count) / sample_rate;
        if (!s.initialized) {
            s.initialized = true;
            s.floor = rms;
        }
        // Follow falling noise rapidly, rising noise only slowly, and never
        // learn a loud active source as ambient while it is speaking.
        if (rms < s.floor) s.floor = rms;
        else if (!s.active) s.floor += std::min(1.0, duration / 20.0) *
                                        (rms - s.floor);
        s.age += duration;
        const double snr = rms - s.floor;
        const bool detected = snr >= (s.active ? 2.5 : 5.0);
        if (detected) {
            s.silence = 0.0;
            s.active = true;
        } else {
            s.silence += duration;
            if (s.age >= 1.5 && s.silence >= 2.5)
                s.active = false;
        }
        return s.active;
    }

    bool present(std::uint64_t id) const noexcept {
        const auto it = states_.find(id);
        return it == states_.end() || it->second.active;
    }

    void clear() noexcept { states_.clear(); }

private:
    struct State {
        bool initialized{false};
        bool active{true};
        double floor{-180.0};
        double age{0.0};
        double silence{0.0};
    };
    std::map<std::uint64_t, State> states_;
};

} // namespace demo
