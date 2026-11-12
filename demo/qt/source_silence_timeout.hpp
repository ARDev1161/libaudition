#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>

namespace demo {

// Fixed level, per-source silence timeout used only by the Qt live adapter.
// Native ODAS tracking and the speech VAD are not modified. A dormant native
// ID can rearm with stronger sound, starting a new recording/ASR generation.
// This is an uncalibrated diagnostic gate: quiet speech may be retired.
class SourceSilenceTimeout final {
public:
    SourceSilenceTimeout(std::uint32_t rate, double seconds = 3.0,
                         double threshold = -55.0, double resume_margin = 6.0)
        : threshold_(threshold), resume_(threshold + resume_margin) {
        if (!rate || !std::isfinite(seconds) || seconds < 0.0 ||
            seconds > 60.0 || !std::isfinite(threshold) ||
            threshold < -120.0 || threshold > -12.0 ||
            !std::isfinite(resume_margin) || resume_margin < 0.0 ||
            resume_margin > 12.0 || resume_ > 0.0)
            throw std::invalid_argument("Invalid source silence timeout");
        limit_ = static_cast<std::uint64_t>(std::ceil(seconds * rate));
    }

    [[nodiscard]] bool observe(std::uint64_t id, const float* pcm,
                               std::size_t size) {
        if (size && !pcm) throw std::invalid_argument("Missing separated PCM");
        if (!limit_) return true;
        auto& state = states_[id];
        if (!size) return !state.dormant;
        double power = 0.0;
        for (std::size_t i = 0; i < size; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite separated PCM");
            power += static_cast<double>(pcm[i]) * pcm[i];
        }
        const double rms = std::sqrt(power / static_cast<double>(size));
        const double dbfs = rms > 0.0 ? 20.0 * std::log10(rms) : -180.0;
        if (state.dormant) {
            if (dbfs >= resume_) state = {};
            return !state.dormant;
        }
        if (dbfs >= threshold_) state.silent = 0U;
        else advance(state, size);
        return !state.dormant;
    }

    [[nodiscard]] bool observeMissing(std::uint64_t id, std::size_t frames) {
        if (!limit_) return true;
        auto& state = states_[id];
        if (!state.dormant) advance(state, frames);
        return !state.dormant;
    }

    void retain(const std::set<std::uint64_t>& present) {
        for (auto it = states_.begin(); it != states_.end();) {
            if (!present.count(it->first)) it = states_.erase(it);
            else ++it;
        }
    }
    void reset() noexcept { states_.clear(); }

private:
    struct State { std::uint64_t silent{0U}; bool dormant{false}; };
    void advance(State& state, std::size_t count) {
        const auto remaining = limit_ - state.silent;
        state.silent += std::min<std::uint64_t>(remaining, count);
        if (state.silent == limit_) state.dormant = true;
    }
    std::map<std::uint64_t, State> states_;
    std::uint64_t limit_{0U};
    double threshold_;
    double resume_;
};
} // namespace demo
