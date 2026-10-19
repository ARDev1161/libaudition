#pragma once

#include <audition/core/id.hpp>
#include <audition/core/time.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace audition::temporal {

// Per-source/per-label recurrence evidence from externally detected event
// onsets. No audio sampling, source identification, or model-dependent scores.
struct RecurrenceOptions {
    std::size_t max_sources{16};
    std::size_t max_labels_per_source{64};
    std::size_t max_onsets_per_label{32};
    std::size_t min_intervals{3};
    // Relative median absolute deviation threshold; 0 requires exact spacing.
    double max_relative_jitter{0.20};
    // Maximum event-label size in bytes, including UTF-8 multibyte data.
    std::size_t max_label_bytes{256};
};
struct RecurrenceEstimate {
    std::size_t intervals{0};
    Duration median_period{};
    Duration median_absolute_deviation{};
    bool periodic{false};
};

class RecurrenceAnalyzer final {
public:
    explicit RecurrenceAnalyzer(RecurrenceOptions options = {}) : options_(options) {
        if (!options.max_sources || !options.max_labels_per_source ||
            options.max_onsets_per_label < 2 ||
            !options.min_intervals || !options.max_label_bytes ||
            options.min_intervals >= options.max_onsets_per_label ||
            !std::isfinite(options.max_relative_jitter) ||
            options.max_relative_jitter < 0)
            throw std::invalid_argument("invalid recurrence options");
    }

    [[nodiscard]] RecurrenceEstimate observe(AcousticSourceId source,
                                             const std::string& label,
                                             Timestamp onset) {
        if (!source.valid() || label.empty() || label.size()>options_.max_label_bytes)
            throw std::invalid_argument("invalid recurrence source/label");
        auto source_it = states_.find(source);
        if (source_it == states_.end() && states_.size() >= options_.max_sources)
            throw std::length_error("recurrence source capacity");
        if (source_it != states_.end()) {
            auto label_it = source_it->second.find(label);
            if (label_it == source_it->second.end() &&
                source_it->second.size() >= options_.max_labels_per_source)
                throw std::length_error("recurrence label capacity");
            if (label_it != source_it->second.end() && !label_it->second.empty()) {
                const auto last = label_it->second.back();
                if (!onset.comparableWith(last) ||
                    onset.nanoseconds() <= last.nanoseconds())
                    throw std::invalid_argument("recurrence time regression");
                // Duration is signed; reject intervals that cannot fit.
                if (static_cast<std::uint64_t>(onset.nanoseconds()) -
                    static_cast<std::uint64_t>(last.nanoseconds()) >
                    static_cast<std::uint64_t>(INT64_MAX))
                    throw std::overflow_error("recurrence interval exceeds duration range");
            }
        }
        auto onsets = source_it == states_.end() ? std::deque<Timestamp>{} :
            (source_it->second.count(label) ? source_it->second.at(label) : std::deque<Timestamp>{});
        onsets.push_back(onset);
        if (onsets.size() > options_.max_onsets_per_label) onsets.pop_front();

        std::vector<std::int64_t> gaps;
        for (std::size_t i=1; i<onsets.size(); ++i) {
            const auto delta = static_cast<std::uint64_t>(onsets[i].nanoseconds()) -
                               static_cast<std::uint64_t>(onsets[i-1].nanoseconds());
            gaps.push_back(static_cast<std::int64_t>(delta));
        }
        RecurrenceEstimate result{};
        result.intervals = gaps.size();
        if (!gaps.empty()) {
            auto median = [](std::vector<std::int64_t> values) {
                const auto mid = values.begin() + static_cast<std::ptrdiff_t>((values.size()-1)/2);
                std::nth_element(values.begin(), mid, values.end());
                return *mid; // lower median avoids int64 averaging overflow
            };
            const auto period = median(gaps);
            std::vector<std::int64_t> errors;
            errors.reserve(gaps.size());
            for (const auto gap: gaps) {
                const auto difference = gap >= period ? gap-period : period-gap;
                errors.push_back(difference);
            }
            const auto mad = median(errors);
            result.median_period = Duration{period};
            result.median_absolute_deviation = Duration{mad};
            result.periodic = result.intervals >= options_.min_intervals &&
                static_cast<double>(mad)/static_cast<double>(period) <= options_.max_relative_jitter;
        }
        states_[source][label] = std::move(onsets);
        return result;
    }

    void forget(AcousticSourceId source) noexcept { states_.erase(source); }
    void reset() noexcept { states_.clear(); }
    [[nodiscard]] std::size_t sourceCount() const noexcept { return states_.size(); }

private:
    RecurrenceOptions options_;
    std::map<AcousticSourceId, std::map<std::string, std::deque<Timestamp>>> states_;
};

} // namespace audition::temporal
