#include <audition/pipeline/temporal_acoustic_event_detector.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>

#include <audition/core/error.hpp>

namespace audition {
namespace {

TemporalAcousticEvent transition(
    TemporalAcousticEventKind kind, AcousticSourceId source_id,
    const std::string& label, std::uint64_t occurrence,
    Timestamp start, Timestamp supported, Probability peak) {
    return {kind, source_id, label, occurrence, start, supported, peak};
}

void requireValidSource(AcousticSourceId id) {
    if (!id.valid()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Temporal detector requires a valid AcousticSourceId"};
    }
}

void requireOrdered(Timestamp now, Timestamp previous, bool allow_equal) {
    if (!now.comparableWith(previous)) {
        throw Error{ErrorCode::ClockDomainMismatch,
                    "Temporal detector observation clock changed"};
    }
    if (now.nanoseconds() < previous.nanoseconds() ||
        (!allow_equal && now.nanoseconds() == previous.nanoseconds())) {
        throw Error{ErrorCode::InvalidArgument,
                    "Temporal detector requires increasing observation timestamps"};
    }
}

// Unsigned difference is well-defined even across negative/positive int64 times,
// after timestamp ordering has been checked.
std::uint64_t elapsedNanoseconds(Timestamp newer, Timestamp older) noexcept {
    return static_cast<std::uint64_t>(newer.nanoseconds()) -
           static_cast<std::uint64_t>(older.nanoseconds());
}

}  // namespace

TemporalAcousticEventDetector::TemporalAcousticEventDetector(
    TemporalAcousticEventDetectorOptions options)
    : options_(std::move(options)) {
    if (options_.start_threshold.value() <= options_.stop_threshold.value() ||
        options_.min_start_observations == 0U ||
        options_.min_stop_observations == 0U ||
        options_.max_sources == 0U ||
        options_.max_labels_per_source == 0U ||
        options_.max_observation_gap.nanoseconds() < 0) {
        throw Error{ErrorCode::InvalidArgument,
                    "Invalid temporal event detector thresholds or capacity"};
    }
}

std::vector<TemporalAcousticEvent> TemporalAcousticEventDetector::observe(
    AcousticSourceId source_id, Timestamp observation_time,
    const ClassificationResult& classification) {
    requireValidSource(source_id);

    // Validate all label payloads before modifying or creating any source state.
    // A missing label is an explicit zero score; duplicate labels are ambiguous.
    std::map<std::string, Probability> scores;
    for (const auto& value : classification.classes) {
        if (value.label.empty() ||
            !scores.emplace(value.label, value.probability).second) {
            throw Error{ErrorCode::InvalidArgument,
                        "Temporal detector requires unique, nonempty class labels"};
        }
    }

    auto found = sources_.find(source_id);
    if (found == sources_.end() && sources_.size() >= options_.max_sources) {
        throw Error{ErrorCode::InvalidState,
                    "Temporal detector source capacity exceeded"};
    }

    SourceState working = found != sources_.end() ? found->second : SourceState{};
    if (working.last_observation) {
        requireOrdered(observation_time, *working.last_observation, false);
    }

    std::vector<TemporalAcousticEvent> changes;
    auto next_id = next_occurrence_id_;

    const auto end = [&](const std::string& label, const LabelState& state) {
        changes.push_back(transition(
            TemporalAcousticEventKind::Ended, source_id, label,
            state.occurrence_id, state.start_time,
            state.last_supported, state.peak));
    };
    const auto start = [&](const std::string& label, LabelState& state,
                           Timestamp time) {
        if (next_id == 0U) {
            throw Error{ErrorCode::InvalidState,
                        "Temporal detector occurrence ID space exhausted"};
        }
        state.occurrence_id = next_id++;
        state.active = true;
        state.start_time = state.candidate_start;
        state.last_supported = time;
        state.low_count = 0U;
        changes.push_back(transition(
            TemporalAcousticEventKind::Started, source_id, label,
            state.occurrence_id, state.start_time,
            state.last_supported, state.peak));
    };

    if (working.last_observation &&
        options_.max_observation_gap.nanoseconds() != 0 &&
        elapsedNanoseconds(observation_time, *working.last_observation) >
            static_cast<std::uint64_t>(options_.max_observation_gap.nanoseconds())) {
        for (const auto& [label, state] : working.labels) {
            if (state.active) end(label, state);
        }
        working.labels.clear();
    }

    // Advance the labels already known to this source. Any omitted class
    // receives score zero. Erase finished/pending-then-rejected labels so
    // long-lived streams cannot accumulate every name they have ever seen.
    for (auto it = working.labels.begin(); it != working.labels.end();) {
        const auto score_it = scores.find(it->first);
        const double score = score_it == scores.end() ? 0.0 :
                             score_it->second.value();
        auto& state = it->second;

        if (!state.active) {
            if (score >= options_.start_threshold.value()) {
                ++state.high_count;
                if (score > state.peak.value()) {
                    state.peak = Probability::from(score);
                }
                if (state.high_count >= options_.min_start_observations) {
                    start(it->first, state, observation_time);
                }
                ++it;
            } else {
                it = working.labels.erase(it);
            }
            continue;
        }

        if (score >= options_.stop_threshold.value()) {
            state.low_count = 0U;
            state.last_supported = observation_time;
            if (score > state.peak.value()) {
                state.peak = Probability::from(score);
            }
            ++it;
        } else {
            ++state.low_count;
            if (state.low_count >= options_.min_stop_observations) {
                end(it->first, state);
                it = working.labels.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Only eligible new labels take space; map ordering keeps emitted Start
    // transitions deterministic even if the classifier changes its label order.
    for (const auto& [label, score] : scores) {
        if (score.value() < options_.start_threshold.value() ||
            working.labels.find(label) != working.labels.end()) {
            continue;
        }
        if (working.labels.size() >= options_.max_labels_per_source) {
            throw Error{ErrorCode::InvalidState,
                        "Temporal detector per-source label capacity exceeded"};
        }
        LabelState state{};
        state.high_count = 1U;
        state.candidate_start = observation_time;
        state.peak = score;
        if (options_.min_start_observations == 1U) {
            start(label, state, observation_time);
        }
        working.labels.emplace(label, std::move(state));
    }

    working.last_observation = observation_time;
    if (found == sources_.end()) {
        sources_.emplace(source_id, std::move(working));
    } else {
        found->second = std::move(working);
    }
    next_occurrence_id_ = next_id;
    return changes;
}

std::vector<TemporalAcousticEvent> TemporalAcousticEventDetector::retire(
    AcousticSourceId source_id, Timestamp time) {
    requireValidSource(source_id);
    const auto found = sources_.find(source_id);
    if (found == sources_.end()) return {};
    if (found->second.last_observation) {
        requireOrdered(time, *found->second.last_observation, true);
    }
    std::vector<TemporalAcousticEvent> changes;
    for (const auto& [label, state] : found->second.labels) {
        if (state.active) {
            changes.push_back(transition(
                TemporalAcousticEventKind::Ended, source_id, label,
                state.occurrence_id, state.start_time,
                state.last_supported, state.peak));
        }
    }
    sources_.erase(found);
    return changes;
}

void TemporalAcousticEventDetector::forget(AcousticSourceId source_id) noexcept {
    sources_.erase(source_id);
}

void TemporalAcousticEventDetector::reset() noexcept {
    sources_.clear();
    // Never recycle emitted occurrence IDs while this detector exists.
}

std::vector<TemporalAcousticEvent>
TemporalAcousticEventDetector::activeEvents() const {
    std::vector<TemporalAcousticEvent> active;
    for (const auto& [source_id, source] : sources_) {
        for (const auto& [label, state] : source.labels) {
            if (state.active) {
                active.push_back(transition(
                    TemporalAcousticEventKind::Started, source_id, label,
                    state.occurrence_id, state.start_time,
                    state.last_supported, state.peak));
            }
        }
    }
    return active;
}

std::size_t TemporalAcousticEventDetector::sourceCount() const noexcept {
    return sources_.size();
}

}  // namespace audition
