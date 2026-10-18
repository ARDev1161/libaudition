#include <audition/temporal/page_hinkley.hpp>
#include <audition/temporal/timed_segmenter.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
audition::Timestamp ts(std::int64_t n) {
    return {n, {audition::ClockDomain::Simulation, 9}};
}
}

TEST(TemporalNumericBoundaries, SegmenterAcrossEntireSignedTimestampRange) {
    audition::temporal::TimedSegmentOptions opts{};
    opts.min_on = audition::Duration{1};
    opts.max_gap = audition::Duration{0};
    audition::temporal::TimedActivitySegmenter segmenter{opts};
    const audition::AcousticSourceId id{7};
    EXPECT_TRUE(segmenter.observe(id, ts(std::numeric_limits<std::int64_t>::min()), true).empty());
    const auto started = segmenter.observe(
        id, ts(std::numeric_limits<std::int64_t>::max()), true);
    ASSERT_EQ(started.size(), 1u);
    EXPECT_EQ(started.front().start.nanoseconds(),
              std::numeric_limits<std::int64_t>::min());
    const auto ended = segmenter.retire(id);
    ASSERT_EQ(ended.size(), 1u);
    EXPECT_EQ(ended.front().last_active.nanoseconds(),
              std::numeric_limits<std::int64_t>::max());
}

TEST(TemporalNumericBoundaries, PageHinkleyOverflowIsTransactional) {
    audition::temporal::PageHinkleyDetector detector;
    const audition::AcousticSourceId id{1};
    const double huge = std::numeric_limits<double>::max();
    const auto initial = detector.observe(id, "energy", ts(1), huge);
    EXPECT_EQ(initial.observations, 1u);
    EXPECT_THROW(static_cast<void>(detector.observe(id, "energy", ts(2), -huge)),
                 std::overflow_error);
    const auto next = detector.observe(id, "energy", ts(2), huge);
    EXPECT_EQ(next.observations, 2u);
    EXPECT_FALSE(next.upward_change);
    EXPECT_FALSE(next.downward_change);
}
