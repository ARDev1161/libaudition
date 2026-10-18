#pragma once
#include <audition/core/id.hpp>
#include <audition/core/time.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace audition::temporal {

// Duration-driven binary segmentation. Assumes the caller has generated a
// binary activity observation for each source at a stable clock identity.
// Event boundaries represent observation timestamps, not physical sound onsets.
enum class SegmentTransitionKind { Started, Ended };
struct SegmentTransition {
    AcousticSourceId source_id{};
    SegmentTransitionKind kind{SegmentTransitionKind::Started};
    std::uint64_t occurrence_id{0};
    Timestamp start{};
    Timestamp last_active{};
};
struct TimedSegmentOptions {
    Duration min_on{0};
    Duration hangover{0};
    Duration max_gap{0}; // 0 disables forced closure
    std::size_t max_sources{16};
};

class TimedActivitySegmenter final {
public:
    explicit TimedActivitySegmenter(TimedSegmentOptions o = {}) : options_(o) {
        if (o.min_on.nanoseconds() < 0 || o.hangover.nanoseconds() < 0 ||
            o.max_gap.nanoseconds() < 0 || o.max_sources == 0)
            throw std::invalid_argument("invalid segmentation options");
    }
    [[nodiscard]] static std::uint64_t elapsed(Timestamp later, Timestamp earlier) noexcept {
        return static_cast<std::uint64_t>(later.nanoseconds()) -
               static_cast<std::uint64_t>(earlier.nanoseconds());
    }
    [[nodiscard]] std::vector<SegmentTransition> observe(
        AcousticSourceId id, Timestamp time, bool active) {
        if (!id.valid()) throw std::invalid_argument("invalid acoustic source");
        auto it = states_.find(id);
        if (it == states_.end() && states_.size() >= options_.max_sources)
            throw std::length_error("segmenter capacity");
        if (it != states_.end() &&
            (!time.comparableWith(it->second.last) ||
             time.nanoseconds() <= it->second.last.nanoseconds()))
            throw std::invalid_argument("segmenter timestamp order");
        std::vector<SegmentTransition> changes;
        State next = it == states_.end() ? State{} : it->second;
        if (next.seen && next.started && options_.max_gap.nanoseconds() > 0 &&
            elapsed(time, next.last) > static_cast<std::uint64_t>(options_.max_gap.nanoseconds())) {
            changes.push_back({id, SegmentTransitionKind::Ended, next.occurrence,
                               next.start, next.last_active});
            next = State{};
        }
        if (next.seen && !next.started && options_.max_gap.nanoseconds() > 0 &&
            elapsed(time, next.last) > static_cast<std::uint64_t>(options_.max_gap.nanoseconds()))
            next = State{};
        if (active) {
            if (!next.candidate) {
                next.candidate = true;
                next.start = time;
            }
            next.last_active = time;
            if (!next.started &&
                elapsed(time, next.start) >= static_cast<std::uint64_t>(options_.min_on.nanoseconds())) {
                if (next_id_ == 0) throw std::overflow_error("segment occurrence exhausted");
                next.occurrence = next_id_;
                next.started = true;
                changes.push_back({id, SegmentTransitionKind::Started,
                                   next.occurrence, next.start, time});
            }
        } else if (next.candidate) {
            if (!next.started) {
                next = State{};
            } else if (elapsed(time, next.last_active) >=
                       static_cast<std::uint64_t>(options_.hangover.nanoseconds())) {
                changes.push_back({id, SegmentTransitionKind::Ended, next.occurrence,
                                   next.start, next.last_active});
                next = State{};
            }
        }
        next.seen = true;
        next.last = time;
        states_[id] = next;
        for (const auto& c : changes)
            if (c.kind == SegmentTransitionKind::Started) ++next_id_;
        return changes;
    }
    [[nodiscard]] std::vector<SegmentTransition> retire(AcousticSourceId id) {
        std::vector<SegmentTransition> events;
        const auto it = states_.find(id);
        if (it == states_.end()) return events;
        if (it->second.started)
            events.push_back({id, SegmentTransitionKind::Ended,
                              it->second.occurrence, it->second.start, it->second.last_active});
        states_.erase(it);
        return events;
    }
    void forget(AcousticSourceId id) noexcept { states_.erase(id); }
    void reset() noexcept { states_.clear(); }
private:
    struct State {
        bool seen{false}, candidate{false}, started{false};
        Timestamp last{}, start{}, last_active{};
        std::uint64_t occurrence{0};
    };
    TimedSegmentOptions options_{};
    std::map<AcousticSourceId, State> states_{};
    std::uint64_t next_id_{1};
};
} // namespace audition::temporal
