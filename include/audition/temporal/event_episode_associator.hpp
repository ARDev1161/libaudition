#pragma once
#include <audition/core/id.hpp>
#include <audition/core/time.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace audition::temporal {

// Associates externally resolved persistent source observations with a
// continuous activity episode. Never uses ephemeral SpatialTrackId or
// guessed semantic class identity as a source match.
struct AssociationObservation {
    AcousticSourceId source{};
    std::string label{};
    Timestamp time{};
    bool active{false};
};
enum class AssociationChangeKind { Started, Continued, Ended };
struct AssociationChange {
    AssociationChangeKind kind{AssociationChangeKind::Started};
    AcousticSourceId source{};
    std::string label{};
    std::uint64_t episode_id{0};
    Timestamp first{};
    Timestamp last_supported{};
};
struct EventAssociationOptions {
    Duration max_gap{0}; // 0: explicitly close only; >0: close on gap
    std::size_t max_sources{16};
    std::size_t max_labels_per_source{64};
};

class EventEpisodeAssociator final {
public:
    explicit EventEpisodeAssociator(EventAssociationOptions options = {}) : options_(options) {
        if (options_.max_gap.nanoseconds() < 0 || !options_.max_sources ||
            !options_.max_labels_per_source)
            throw std::invalid_argument("invalid association options");
    }

    [[nodiscard]] std::vector<AssociationChange> observe(const AssociationObservation& o) {
        if (!o.source.valid() || o.label.empty())
            throw std::invalid_argument("invalid association observation");
        auto src = sources_.find(o.source);
        if (src == sources_.end() && sources_.size() >= options_.max_sources)
            throw std::length_error("association source capacity");
        if (src != sources_.end()) {
            auto item = src->second.find(o.label);
            if (item == src->second.end() &&
                src->second.size() >= options_.max_labels_per_source)
                throw std::length_error("association label capacity");
            if (item != src->second.end() &&
                (!o.time.comparableWith(item->second.last_observation) ||
                 o.time.nanoseconds() <= item->second.last_observation.nanoseconds()))
                throw std::invalid_argument("association timestamp regression");
        }
        State next{};
        if (src != sources_.end()) {
            auto item = src->second.find(o.label);
            if (item != src->second.end()) next = item->second;
        }
        std::vector<AssociationChange> result;
        const bool expired = next.seen && next.active &&
            options_.max_gap.nanoseconds() > 0 &&
            (static_cast<long double>(o.time.nanoseconds()) -
             static_cast<long double>(next.last_observation.nanoseconds())) >
                options_.max_gap.nanoseconds();
        if (expired) {
            result.push_back({AssociationChangeKind::Ended, o.source, o.label,
                              next.episode, next.first, next.last_supported});
            next.active = false;
        }
        if (o.active) {
            if (!next.active) {
                if (!next_episode_) throw std::overflow_error("episode id exhausted");
                next.episode = next_episode_;
                next.first = o.time;
                next.active = true;
                result.push_back({AssociationChangeKind::Started, o.source, o.label,
                                  next.episode, next.first, o.time});
            } else {
                result.push_back({AssociationChangeKind::Continued, o.source, o.label,
                                  next.episode, next.first, o.time});
            }
            next.last_supported = o.time;
        } else if (next.active) {
            result.push_back({AssociationChangeKind::Ended, o.source, o.label,
                              next.episode, next.first, next.last_supported});
            next.active = false;
        }
        next.last_observation = o.time;
        next.seen = true;
        sources_[o.source][o.label] = next;
        for (const auto& c : result)
            if (c.kind == AssociationChangeKind::Started) ++next_episode_;
        return result;
    }

    [[nodiscard]] std::vector<AssociationChange> retire(AcousticSourceId id) {
        std::vector<AssociationChange> out;
        auto it = sources_.find(id);
        if (it == sources_.end()) return out;
        for (const auto& [label, s] : it->second)
            if (s.active)
                out.push_back({AssociationChangeKind::Ended, id, label,
                               s.episode, s.first, s.last_supported});
        sources_.erase(it);
        return out;
    }
    void forget(AcousticSourceId id) noexcept { sources_.erase(id); }
    void reset() noexcept { sources_.clear(); }
    [[nodiscard]] std::size_t sourceCount() const noexcept { return sources_.size(); }

private:
    struct State {
        bool seen{false}, active{false};
        Timestamp first{}, last_supported{}, last_observation{};
        std::uint64_t episode{0};
    };
    EventAssociationOptions options_{};
    std::map<AcousticSourceId, std::map<std::string, State>> sources_{};
    std::uint64_t next_episode_{1};
};
} // namespace audition::temporal
