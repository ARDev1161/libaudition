#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace demo {

// GUI-side liveness indicator, independent of ODAS source identities and
// sound activity. No ALSA/ODAS restart or timeout policy is imposed here.
class LiveCaptureProgressMonitor final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    enum class State { Starting, Flowing, SlowStartup, Stalled };
    struct Status {
        State state{State::Starting};
        std::chrono::milliseconds since_progress{0};
    };

    explicit LiveCaptureProgressMonitor(
        std::chrono::milliseconds startup_timeout = std::chrono::seconds{15},
        std::chrono::milliseconds stall_timeout = std::chrono::seconds{3})
        : startup_timeout_(startup_timeout), stall_timeout_(stall_timeout) {
        if (startup_timeout_.count() <= 0 || stall_timeout_.count() <= 0)
            throw std::invalid_argument("Capture progress timeouts must be positive");
    }

    void reset(TimePoint now) noexcept {
        last_progress_ = now;
        last_hops_ = 0U;
        initialized_ = true;
        received_audio_ = false;
    }

    [[nodiscard]] Status observe(std::uint64_t processed_hops, TimePoint now) noexcept {
        if (!initialized_) reset(now);
        if (processed_hops != last_hops_) {
            // Nonmonotonic counts also indicate a new capture generation;
            // reset timing rather than mistaking it for prolonged inactivity.
            last_hops_ = processed_hops;
            last_progress_ = now;
            received_audio_ = processed_hops > 0U;
        }
        const auto delta = now >= last_progress_
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - last_progress_)
            : std::chrono::milliseconds{0};
        if (!received_audio_)
            return {delta >= startup_timeout_ ? State::SlowStartup
                                              : State::Starting, delta};
        return {delta >= stall_timeout_ ? State::Stalled
                                        : State::Flowing, delta};
    }

private:
    std::chrono::milliseconds startup_timeout_;
    std::chrono::milliseconds stall_timeout_;
    TimePoint last_progress_{};
    std::uint64_t last_hops_{0U};
    bool initialized_{false};
    bool received_audio_{false};
};

} // namespace demo
