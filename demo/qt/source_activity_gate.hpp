#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <set>

namespace demo {

// Visibility of *audible* live ODAS SST tracks. SST may retain an ID after
// its activity falls to zero; this gate does not retire that ID or affect
// SSS, recordings, classifier, VAD or ASR. Offline replay is not gated.
class SourceActivityGate {
public:
    static constexpr double kEnterActivity = 0.03;
    static constexpr double kHoldActivity = 0.01;
    static constexpr std::int64_t kSilenceHoldMs = 800;

    [[nodiscard]] bool visible(std::uint64_t id, double activity,
                               std::int64_t nowMs) {
        const double level = std::isfinite(activity) ? activity : 0.0;
        if (level >= kEnterActivity) {
            states_[id] = {nowMs};
            return true;
        }
        auto entry = states_.find(id);
        if (entry == states_.end()) return false;
        if (level >= kHoldActivity) {
            entry->second.last_audible_ms = nowMs;
            return true;
        }
        if (nowMs >= entry->second.last_audible_ms &&
            nowMs - entry->second.last_audible_ms <= kSilenceHoldMs)
            return true;
        states_.erase(entry);
        return false;
    }

    void retainOnly(const std::set<std::uint64_t>& trackedIds) {
        for (auto iter = states_.begin(); iter != states_.end();) {
            if (!trackedIds.count(iter->first))
                iter = states_.erase(iter);
            else
                ++iter;
        }
    }
    void reset() noexcept { states_.clear(); }

private:
    struct State { std::int64_t last_audible_ms{0}; };
    std::map<std::uint64_t, State> states_{};
};

} // namespace demo
