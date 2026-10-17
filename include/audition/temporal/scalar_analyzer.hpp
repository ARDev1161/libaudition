#pragma once

#include <audition/core/id.hpp>
#include <audition/core/time.hpp>

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

        auto& state = states_[sample.source_id][sample.feature];
        ScalarEvidence result{};
        result.input = sample;
        if (!state.initialized) {
            state.mean = sample.value;
            state.initialized = true;
        } else {
            // CUSUM against the previous smoothed baseline (not the updated one).
            const double residual = sample.value - state.mean;
            const double up = std::max(0.0, state.up + residual - options_.drift);
            const double down = std::max(0.0, state.down - residual - options_.drift);
            result.positive_change = up >= options_.threshold;
            result.negative_change = down >= options_.threshold;
            state.up = result.positive_change ? 0.0 : up;
            state.down = result.negative_change ? 0.0 : down;
            state.mean += options_.alpha * residual;
            if (!std::isfinite(state.mean)) {
                // Finite input alone does not guarantee finite intermediate sums.
                // This path cannot change the preexisting state beyond this point.
                throw std::overflow_error("scalar smoothing overflow");
            }
        }
        state.last = sample.time;
        result.smoothed = state.mean;
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
