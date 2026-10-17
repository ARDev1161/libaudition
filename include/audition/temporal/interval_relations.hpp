#pragma once
#include <audition/core/time.hpp>
#include <stdexcept>

namespace audition::temporal {

// Allen's mutually exclusive 13 temporal interval relationships.
// Timestamp bounds describe supported observations, not guaranteed physical onset.
enum class IntervalRelation {
    Before, Meets, Overlaps, Starts, During, Finishes, Equal,
    FinishedBy, Contains, StartedBy, OverlappedBy, MetBy, After
};
struct EventInterval {
    Timestamp start{};
    Timestamp end{};
};

[[nodiscard]] inline IntervalRelation relate(const EventInterval& a,
                                              const EventInterval& b) {
    if (!a.start.comparableWith(a.end) ||
        !b.start.comparableWith(b.end) ||
        !a.start.comparableWith(b.start) ||
        a.end.nanoseconds() < a.start.nanoseconds() ||
        b.end.nanoseconds() < b.start.nanoseconds()) {
        throw std::invalid_argument("invalid or incompatible temporal intervals");
    }
    const auto as = a.start.nanoseconds(), ae = a.end.nanoseconds();
    const auto bs = b.start.nanoseconds(), be = b.end.nanoseconds();
    if (ae < bs) return IntervalRelation::Before;
    if (ae == bs) return IntervalRelation::Meets;
    if (as > be) return IntervalRelation::After;
    if (as == be) return IntervalRelation::MetBy;
    if (as == bs && ae == be) return IntervalRelation::Equal;
    if (as == bs) return ae < be ? IntervalRelation::Starts : IntervalRelation::StartedBy;
    if (ae == be) return as > bs ? IntervalRelation::Finishes : IntervalRelation::FinishedBy;
    if (as < bs && ae < be) return IntervalRelation::Overlaps;
    if (as > bs && ae > be) return IntervalRelation::OverlappedBy;
    return as > bs ? IntervalRelation::During : IntervalRelation::Contains;
}
}  // namespace audition::temporal
