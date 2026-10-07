#pragma once

#include <chrono>
#include <cstdint>

#include <audition/core/error.hpp>
#include <audition/core/export.hpp>

namespace audition {

enum class ClockDomain : std::uint8_t {
    SystemUtc,
    Monotonic,
    Simulation,
    External,
};

struct ClockIdentity {
    ClockDomain domain{ClockDomain::Monotonic};
    std::uint32_t source_id{0};

    friend constexpr bool operator==(const ClockIdentity& lhs, const ClockIdentity& rhs) noexcept {
        return lhs.domain == rhs.domain && lhs.source_id == rhs.source_id;
    }
    friend constexpr bool operator!=(const ClockIdentity& lhs, const ClockIdentity& rhs) noexcept {
        return !(lhs == rhs);
    }
};

class Duration {
public:
    constexpr Duration() noexcept = default;
    explicit constexpr Duration(std::int64_t nanoseconds) noexcept : nanoseconds_(nanoseconds) {}

    template <typename Rep, typename Period>
    static constexpr Duration fromChrono(std::chrono::duration<Rep, Period> value) noexcept {
        return Duration{std::chrono::duration_cast<std::chrono::nanoseconds>(value).count()};
    }

    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return nanoseconds_; }
    [[nodiscard]] constexpr double seconds() const noexcept {
        return static_cast<double>(nanoseconds_) / 1'000'000'000.0;
    }
    [[nodiscard]] constexpr std::chrono::nanoseconds chrono() const noexcept {
        return std::chrono::nanoseconds{nanoseconds_};
    }

    friend constexpr Duration operator+(Duration lhs, Duration rhs) noexcept {
        return Duration{lhs.nanoseconds_ + rhs.nanoseconds_};
    }
    friend constexpr Duration operator-(Duration lhs, Duration rhs) noexcept {
        return Duration{lhs.nanoseconds_ - rhs.nanoseconds_};
    }

private:
    std::int64_t nanoseconds_{0};
};

class Timestamp {
public:
    constexpr Timestamp() noexcept = default;
    constexpr Timestamp(std::int64_t nanoseconds, ClockIdentity clock) noexcept
        : nanoseconds_(nanoseconds), clock_(clock) {}

    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return nanoseconds_; }
    [[nodiscard]] constexpr ClockIdentity clock() const noexcept { return clock_; }
    [[nodiscard]] constexpr bool comparableWith(const Timestamp& other) const noexcept {
        return clock_ == other.clock_;
    }

    [[nodiscard]] Duration since(const Timestamp& earlier) const;
    [[nodiscard]] Timestamp advancedBy(Duration duration) const noexcept;

    static Timestamp systemNow();
    static Timestamp monotonicNow();

private:
    std::int64_t nanoseconds_{0};
    ClockIdentity clock_{};
};

}  // namespace audition
