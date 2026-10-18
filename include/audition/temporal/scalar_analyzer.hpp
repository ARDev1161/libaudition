#pragma once

#include <audition/core/id.hpp>
#include <audition/core/time.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace audition::temporal {

// Input scalar values must share stable semantics within (source, feature).
// This class never conflates different sources and never uses inference-completion time.
struct ScalarObservation {
    AcousticSourceId source_id{};
    std::string feature{};
    Timestamp time{};
    double value{0.0};
};

struct ScalarEvidence {
    ScalarObservation input{};
    double smoothed{0.0};
    bool positive_change{false};
    bool negative_change{false};
};

struct TemporalScalarOptions {
    // EMA smoothing, 1.0 disables smoothing.
    double alpha{0.35};
    // One-sided CUSUM reference offset and decision interval, in feature units.
    double drift{0.05};
    double threshold{1.0};
    std::size_t max_sources{16};
    std::size_t max_features_per_source{64};
};

class TemporalScalarAnalyzer final {
public:
    explicit TemporalScalarAnalyzer(TemporalScalarOptions options = {})
        : options_(options) {
        if (!(options_.alpha > 0.0 && options_.alpha <= 1.0) ||
            !std::isfinite(options_.alpha) || !std::isfinite(options_.drift) ||
            options_.drift < 0.0 || !std::isfinite(options_.threshold) ||
            options_.threshold <= 0.0 || options_.max_sources == 0 ||
            options_.max_features_per_source == 0) {
            throw std::invalid_argument("invalid temporal scalar options");
        }
    }

    // Returns exactly one observation; change alarms are *not* event onset estimates.
    // All invalid samples fail before state mutation.
    [[nodiscard]] ScalarEvidence observe(const ScalarObservation& sample) {
        if (!sample.source_id.valid() || sample.feature.empty() ||
            !std::isfinite(sample.value)) {
            throw std::invalid_argument("invalid scalar observation");
        }
        auto source = states_.find(sample.source_id);
        if (source == states_.end() && states_.size() == options_.max_sources) {
            throw std::length_error("temporal source capacity reached");
        }
        if (source != states_.end()) {
            auto feature = source->second.find(sample.feature);
            if (feature == source->second.end() &&
                source->second.size() == options_.max_features_per_source) {
                throw std::length_error("temporal feature capacity reached");
            }
            if (feature != source->second.end()) {
                const auto& last = feature->second.last;
                if (!sample.time.comparableWith(last) ||
                    sample.time.nanoseconds() <= last.nanoseconds()) {
                    throw std::invalid_argument("nonmonotonic scalar timestamp");
                }
            }
        }

        State next{};
        if (source != states_.end()) {
            const auto existing = source->second.find(sample.feature);
            if (existing != source->second.end()) next = existing->second;
        }
        ScalarEvidence result{};
        result.input = sample;
        if (!next.initialized) {
            next.mean = sample.value;
            next.initialized = true;
        } else {
            // The entire numerical update is computed locally and committed
            // only after checking all finite intermediate values.
            const double residual = sample.value - next.mean;
            const double up_raw = next.up + residual - options_.drift;
            const double down_raw = next.down - residual - options_.drift;
            const double next_mean = next.mean + options_.alpha * residual;
            if (!std::isfinite(residual) || !std::isfinite(up_raw) ||
                !std::isfinite(down_raw) || !std::isfinite(next_mean))
                throw std::overflow_error("scalar temporal arithmetic overflow");
            const double up = std::max(0.0, up_raw);
            const double down = std::max(0.0, down_raw);
            result.positive_change = up >= options_.threshold;
            result.negative_change = down >= options_.threshold;
            next.mean = next_mean;
            next.up = result.positive_change ? 0.0 : up;
            next.down = result.negative_change ? 0.0 : down;
        }
        next.last = sample.time;
        result.smoothed = next.mean;
        states_[sample.source_id][sample.feature] = next;
        return result;
    }

    void forget(AcousticSourceId source) noexcept { states_.erase(source); }
    void reset() noexcept { states_.clear(); }
    [[nodiscard]] std::size_t sourceCount() const noexcept { return states_.size(); }

private:
    struct State {
        bool initialized{false};
        Timestamp last{};
        double mean{0.0};
        double up{0.0};
        double down{0.0};
    };
    TemporalScalarOptions options_;
    std::map<AcousticSourceId, std::map<std::string, State>> states_;
};

}  // namespace audition::temporal
