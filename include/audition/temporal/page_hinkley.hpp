#pragma once
#include <audition/core/id.hpp>
#include <audition/core/time.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>

namespace audition::temporal {

// Page-Hinkley detects sustained shifts in a scalar sequence. Its threshold
// and allowance are in the same units as the supplied feature.
struct PageHinkleyOptions {
    double allowance{0.025};
    double threshold{1.0};
    std::size_t min_observations{3};
    std::size_t max_sources{16};
    std::size_t max_features_per_source{64};
};
struct PageHinkleyResult {
    double running_mean{0.0};
    bool upward_change{false};
    bool downward_change{false};
    std::size_t observations{0};
};

class PageHinkleyDetector final {
public:
    explicit PageHinkleyDetector(PageHinkleyOptions options = {}) : options_(options) {
        if (!std::isfinite(options_.allowance) || options_.allowance < 0 ||
            !std::isfinite(options_.threshold) || options_.threshold <= 0 ||
            !options_.min_observations || !options_.max_sources ||
            !options_.max_features_per_source)
            throw std::invalid_argument("invalid Page-Hinkley options");
    }

    [[nodiscard]] PageHinkleyResult observe(AcousticSourceId source,
                                            const std::string& feature,
                                            Timestamp time, double value) {
        if (!source.valid() || feature.empty() || !std::isfinite(value))
            throw std::invalid_argument("invalid Page-Hinkley sample");
        auto src = states_.find(source);
        if (src == states_.end() && states_.size() >= options_.max_sources)
            throw std::length_error("Page-Hinkley source capacity");
        if (src != states_.end()) {
            auto it = src->second.find(feature);
            if (it == src->second.end() &&
                src->second.size() >= options_.max_features_per_source)
                throw std::length_error("Page-Hinkley feature capacity");
            if (it != src->second.end() &&
                (!time.comparableWith(it->second.last) ||
                 time.nanoseconds() <= it->second.last.nanoseconds()))
                throw std::invalid_argument("Page-Hinkley timestamp order");
        }

        // Compute locally before committing to preserve state on overflow.
        State next{};
        if (src != states_.end()) {
            auto it = src->second.find(feature);
            if (it != src->second.end()) next = it->second;
        }
        if (next.count == static_cast<std::size_t>(-1))
            throw std::overflow_error("Page-Hinkley observation count overflow");
        const double delta = value - next.mean;
        const auto count = next.count + 1;
        const double mean = next.mean + delta / static_cast<double>(count);
        const double up = next.up + value - mean - options_.allowance;
        const double down = next.down + value - mean + options_.allowance;
        if (!std::isfinite(mean) || !std::isfinite(up) || !std::isfinite(down))
            throw std::overflow_error("Page-Hinkley arithmetic overflow");
        next.count = count;
        next.mean = mean;
        next.up = up;
        next.down = down;
        next.min_up = std::min(next.min_up, up);
        next.max_down = std::max(next.max_down, down);
        const double upward_span = up - next.min_up;
        const double downward_span = next.max_down - down;
        if (!std::isfinite(upward_span) || !std::isfinite(downward_span))
            throw std::overflow_error("Page-Hinkley cumulative span overflow");
        const bool ready = count >= options_.min_observations;
        const bool rise = ready && upward_span >= options_.threshold;
        const bool fall = ready && downward_span >= options_.threshold;
        if (rise || fall) {
            next.up = next.down = next.min_up = next.max_down = 0;
            next.mean = value;
            next.count = 1;
        }
        next.last = time;
        states_[source][feature] = next;
        return {mean, rise, fall, count};
    }

    void forget(AcousticSourceId source) noexcept { states_.erase(source); }
    void reset() noexcept { states_.clear(); }
private:
    struct State {
        Timestamp last{};
        double mean{0}, up{0}, down{0}, min_up{0}, max_down{0};
        std::size_t count{0};
    };
    PageHinkleyOptions options_{};
    std::map<AcousticSourceId, std::map<std::string, State>> states_{};
};
} // namespace audition::temporal
