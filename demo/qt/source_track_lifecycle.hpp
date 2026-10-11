#pragma once

#include <cstdint>
#include <set>
#include <utility>

namespace demo {

// Tracks identities present in each ODAS processing frame. Retiring a missing
// source invalidates its classification generation before ODAS can reuse the ID.
// SSS audio is included in the current set even if the tracker snapshot lags.
class SourceTrackLifecycle final {
public:
    template <typename OnRetire>
    void update(std::set<std::uint64_t> current, OnRetire&& retire) {
        for (const auto id : active_) {
            if (current.find(id) == current.end()) {
                retire(id);
            }
        }
        active_.swap(current);
    }

    template <typename OnRetire>
    void clear(OnRetire&& retire) {
        for (const auto id : active_) {
            retire(id);
        }
        active_.clear();
    }

private:
    std::set<std::uint64_t> active_{};
};

} // namespace demo
