#include <audition/temporal/recurrence_analyzer.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
audition::Timestamp t(std::int64_t n, unsigned clock = 1) {
    return {n, {audition::ClockDomain::Simulation,clock}};
}
audition::AcousticSourceId src(unsigned n=1) { return audition::AcousticSourceId{n}; }
}
TEST(RecurrenceAnalyzer, StablePeriodAfterMinimumIntervals) {
    audition::temporal::RecurrenceAnalyzer analyzer;
    for (int i=0;i<3;++i) {
        const auto estimate=analyzer.observe(src(), "alarm", t(i*100));
        EXPECT_FALSE(estimate.periodic);
    }
    const auto estimate=analyzer.observe(src(), "alarm", t(300));
    EXPECT_TRUE(estimate.periodic);
    EXPECT_EQ(estimate.intervals,3u);
    EXPECT_EQ(estimate.median_period.nanoseconds(),100);
    EXPECT_EQ(estimate.median_absolute_deviation.nanoseconds(),0);
}
TEST(RecurrenceAnalyzer, IrregularSequenceIsNotPeriodic) {
    audition::temporal::RecurrenceAnalyzer analyzer;
    for (const int i: {0,100,350,400})
        (void)analyzer.observe(src(), "alarm", t(i));
    const auto e=analyzer.observe(src(), "alarm", t(900));
    EXPECT_FALSE(e.periodic);
}
TEST(RecurrenceAnalyzer, IsolatesLabelsAndSources) {
    audition::temporal::RecurrenceAnalyzer analyzer;
    (void)analyzer.observe(src(1), "alarm", t(1));
    (void)analyzer.observe(src(2), "alarm", t(1,2));
    auto other=analyzer.observe(src(1), "speech", t(2));
    EXPECT_EQ(other.intervals,0u);
    EXPECT_EQ(analyzer.sourceCount(),2u);
    analyzer.forget(src(1));
    EXPECT_EQ(analyzer.sourceCount(),1u);
}
TEST(RecurrenceAnalyzer, RejectsBadClockAndRetainsState) {
    audition::temporal::RecurrenceAnalyzer analyzer;
    (void)analyzer.observe(src(), "bell", t(10));
    EXPECT_THROW(static_cast<void>(analyzer.observe(src(), "bell", t(11,2))),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(analyzer.observe(src(), "bell", t(10))),std::invalid_argument);
    EXPECT_EQ(analyzer.observe(src(), "bell", t(20)).intervals,1u);
}
TEST(RecurrenceAnalyzer, RejectsIntervalBeyondSignedDuration) {
    audition::temporal::RecurrenceAnalyzer analyzer;
    (void)analyzer.observe(src(), "bell", t(std::numeric_limits<std::int64_t>::min()));
    EXPECT_THROW(static_cast<void>(analyzer.observe(src(), "bell",
        t(std::numeric_limits<std::int64_t>::max()))),std::overflow_error);
    EXPECT_EQ(analyzer.observe(src(), "bell",
        t(std::numeric_limits<std::int64_t>::min()+10)).median_period.nanoseconds(),10);
}
TEST(RecurrenceAnalyzer, BoundedHistoryAndCapacity) {
    audition::temporal::RecurrenceOptions opts{};
    opts.max_sources=1;
    opts.max_labels_per_source=1;
    opts.max_onsets_per_label=4;
    opts.min_intervals=2;
    audition::temporal::RecurrenceAnalyzer analyzer(opts);
    for (int i=0;i<8;++i)
        (void)analyzer.observe(src(), "bell", t(i*10));
    EXPECT_EQ(analyzer.observe(src(), "bell", t(80)).intervals,3u);
    EXPECT_THROW(static_cast<void>(analyzer.observe(src(2),"bell",t(1))),std::length_error);
    EXPECT_THROW(static_cast<void>(analyzer.observe(src(),"other",t(90))),std::length_error);
}
