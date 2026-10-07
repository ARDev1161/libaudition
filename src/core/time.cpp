#include <audition/core/time.hpp>

namespace audition {

Duration Timestamp::since(const Timestamp& earlier) const {
    if (!comparableWith(earlier)) {
        throw Error{ErrorCode::ClockDomainMismatch,
                    "Cannot subtract timestamps from different clock domains"};
    }
    return Duration{nanoseconds_ - earlier.nanoseconds_};
}

Timestamp Timestamp::advancedBy(Duration duration) const noexcept {
    return Timestamp{nanoseconds_ + duration.nanoseconds(), clock_};
}

Timestamp Timestamp::systemNow() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return Timestamp{std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(),
                     ClockIdentity{ClockDomain::SystemUtc, 0}};
}

Timestamp Timestamp::monotonicNow() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return Timestamp{std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(),
                     ClockIdentity{ClockDomain::Monotonic, 0}};
}

}  // namespace audition
