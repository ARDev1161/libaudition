#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace audition::dsp {

// Speech-processing errors are separate from recorder backpressure and ASR
// queue drops. All counters refer to a single active source generation.
struct SourceSpeechFailureStats {
    std::uint64_t failed_hops{0};
    std::uint64_t muted_samples{0};
    std::string last_error{};
};

// Non-realtime producer-owned metrics for NS/AGC failure recovery.
// Memory is bounded by max_sources and max_error_bytes (per source).
// Retire before reusing a numeric source ID; totalFailures() is session-wide.
// recordFailure() may allocate and is NOT suitable for hard RT callbacks.
class SourceSpeechFailureTracker final {
public:
    explicit SourceSpeechFailureTracker(std::size_t max_sources = 64U,
                                        std::size_t max_error_bytes = 160U)
        : max_sources_(max_sources), max_error_bytes_(max_error_bytes) {
        if (max_sources == 0U || max_error_bytes == 0U)
            throw std::invalid_argument("Speech failure tracker limits must be positive");
    }

    void recordFailure(std::uint64_t source_id, std::size_t samples,
                       std::string_view error) {
        total_failures_ = increment(total_failures_, 1U);
        auto it = active_.find(source_id);
        if (it == active_.end()) {
            if (active_.size() >= max_sources_) {
                ++untracked_failures_;
                return;
            }
            it = active_.emplace(source_id, SourceSpeechFailureStats{}).first;
        }
        auto& info = it->second;
        info.failed_hops = increment(info.failed_hops, 1U);
        info.muted_samples = increment(info.muted_samples,
                                        static_cast<std::uint64_t>(samples));
        info.last_error.assign(error.data(), std::min(error.size(), max_error_bytes_));
    }

    void retire(std::uint64_t source_id) noexcept { active_.erase(source_id); }
    void reset() noexcept {
        active_.clear();
        total_failures_ = 0U;
        untracked_failures_ = 0U;
    }
    [[nodiscard]] std::uint64_t totalFailures() const noexcept {
        return total_failures_;
    }
    [[nodiscard]] std::uint64_t untrackedFailures() const noexcept {
        return untracked_failures_;
    }
    [[nodiscard]] const std::map<std::uint64_t, SourceSpeechFailureStats>&
    active() const noexcept { return active_; }

private:
    static std::uint64_t increment(std::uint64_t current,
                                   std::uint64_t amount) noexcept {
        const auto max = std::numeric_limits<std::uint64_t>::max();
        return amount > max - current ? max : current + amount;
    }
    std::size_t max_sources_;
    std::size_t max_error_bytes_;
    std::uint64_t total_failures_{0};
    std::uint64_t untracked_failures_{0};
    std::map<std::uint64_t, SourceSpeechFailureStats> active_{};
};

} // namespace audition::dsp
