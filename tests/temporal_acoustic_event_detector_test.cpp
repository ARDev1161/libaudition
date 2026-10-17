#include <audition/pipeline/temporal_acoustic_event_detector.hpp>
#include <audition/pipeline/acoustic_event_assembler.hpp>
#include <audition/core/error.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

audition::Timestamp at(std::int64_t nanos, std::uint32_t clock_id = 1U) {
    return {nanos, {audition::ClockDomain::Simulation, clock_id}};
}

audition::ClassificationResult scores(
    std::initializer_list<std::pair<std::string, double>> entries) {
    audition::ClassificationResult result{};
    for (const auto& [label, value] : entries) {
        result.classes.push_back({label, audition::Probability::from(value)});
    }
    return result;
}

audition::AcousticSourceId source(std::uint64_t id) {
    return audition::AcousticSourceId{id};
}

audition::TemporalAcousticEventDetectorOptions immediate() {
    audition::TemporalAcousticEventDetectorOptions opts{};
    opts.min_start_observations = 1U;
    opts.min_stop_observations = 1U;
    return opts;
}

}  // namespace

TEST(TemporalAcousticEventDetector, HysteresisMergesBriefConfidenceDrops) {
    audition::TemporalAcousticEventDetector detector{};
    const auto id = source(7U);
    EXPECT_TRUE(detector.observe(id, at(10), scores({{"alarm", 0.75}})).empty());
    auto transitions = detector.observe(id, at(11), scores({{"alarm", 0.90}}));
    ASSERT_EQ(transitions.size(), 1U);
    const auto started = transitions[0];
    EXPECT_EQ(started.kind, audition::TemporalAcousticEventKind::Started);
    EXPECT_EQ(started.source_id, id);
    EXPECT_EQ(started.label, "alarm");
    EXPECT_EQ(started.start_time.nanoseconds(), 10);
    EXPECT_EQ(started.last_supported_time.nanoseconds(), 11);
    EXPECT_DOUBLE_EQ(started.peak_score.value(), 0.90);

    EXPECT_TRUE(detector.observe(id, at(12), scores({{"alarm", 0.50}})).empty());
    EXPECT_TRUE(detector.observe(id, at(13), scores({{"alarm", 0.10}})).empty());
    EXPECT_TRUE(detector.observe(id, at(14), scores({{"alarm", 0.80}})).empty());
    EXPECT_TRUE(detector.observe(id, at(15), scores({{"alarm", 0.10}})).empty());
    transitions = detector.observe(id, at(16), scores({{"alarm", 0.10}}));
    ASSERT_EQ(transitions.size(), 1U);
    EXPECT_EQ(transitions[0].kind, audition::TemporalAcousticEventKind::Ended);
    EXPECT_EQ(transitions[0].occurrence_id, started.occurrence_id);
    EXPECT_EQ(transitions[0].start_time.nanoseconds(), 10);
    EXPECT_EQ(transitions[0].last_supported_time.nanoseconds(), 14);
    EXPECT_DOUBLE_EQ(transitions[0].peak_score.value(), 0.90);
    EXPECT_TRUE(detector.activeEvents().empty());
}

TEST(TemporalAcousticEventDetector, StartRequiresConsecutiveHighObservations) {
    audition::TemporalAcousticEventDetector detector{};
    const auto id = source(1U);
    EXPECT_TRUE(detector.observe(id, at(1), scores({{"door", 0.8}})).empty());
    EXPECT_TRUE(detector.observe(id, at(2), scores({{"door", 0.6}})).empty());
    EXPECT_TRUE(detector.observe(id, at(3), scores({{"door", 0.8}})).empty());
    const auto start = detector.observe(id, at(4), scores({{"door", 0.7}}));
    ASSERT_EQ(start.size(), 1U);
    EXPECT_EQ(start[0].start_time.nanoseconds(), 3);
    EXPECT_DOUBLE_EQ(start[0].peak_score.value(), 0.8);
    EXPECT_TRUE(detector.observe(id, at(5), scores({})).empty());
    const auto stop = detector.observe(id, at(6), scores({}));
    ASSERT_EQ(stop.size(), 1U);
    EXPECT_EQ(stop[0].last_supported_time.nanoseconds(), 4);
}

TEST(TemporalAcousticEventDetector, DifferentSourcesCanActivateSameClassIndependently) {
    auto opts = immediate();
    audition::TemporalAcousticEventDetector detector{opts};
    const auto first = detector.observe(source(2U), at(1), scores({{"speech", 0.91}}));
    const auto second = detector.observe(source(8U), at(1), scores({{"speech", 0.92}}));
    ASSERT_EQ(first.size(), 1U);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_NE(first[0].occurrence_id, second[0].occurrence_id);
    EXPECT_EQ(detector.sourceCount(), 2U);
    auto active = detector.activeEvents();
    ASSERT_EQ(active.size(), 2U);
    EXPECT_EQ(active[0].source_id, source(2U));
    EXPECT_EQ(active[1].source_id, source(8U));

    const auto stop = detector.observe(source(2U), at(2), scores({}));
    ASSERT_EQ(stop.size(), 1U);
    EXPECT_EQ(stop[0].source_id, source(2U));
    active = detector.activeEvents();
    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(active[0].source_id, source(8U));
}

TEST(TemporalAcousticEventDetector, MultipleClassesAreSortedAndIndependent) {
    audition::TemporalAcousticEventDetector detector{immediate()};
    const auto events = detector.observe(source(9U), at(10),
                                         scores({{"zebra", 0.9}, {"alarm", 0.8},
                                                 {"quiet", 0.2}}));
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].label, "alarm");
    EXPECT_EQ(events[1].label, "zebra");
    auto stop = detector.observe(source(9U), at(11),
                                 scores({{"zebra", 0.8}, {"alarm", 0.3}}));
    ASSERT_EQ(stop.size(), 1U);
    EXPECT_EQ(stop[0].label, "alarm");
    auto active = detector.activeEvents();
    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(active.front().label, "zebra");
}

TEST(TemporalAcousticEventDetector, GapEndsOldOccurrenceBeforeNewOne) {
    auto opts = immediate();
    opts.max_observation_gap = audition::Duration{3};
    audition::TemporalAcousticEventDetector detector{opts};
    const auto before = detector.observe(source(5U), at(10), scores({{"engine", 0.8}}));
    ASSERT_EQ(before.size(), 1U);
    const auto after = detector.observe(source(5U), at(14), scores({{"engine", 0.9}}));
    ASSERT_EQ(after.size(), 2U);
    EXPECT_EQ(after[0].kind, audition::TemporalAcousticEventKind::Ended);
    EXPECT_EQ(after[0].occurrence_id, before[0].occurrence_id);
    EXPECT_EQ(after[0].last_supported_time.nanoseconds(), 10);
    EXPECT_EQ(after[1].kind, audition::TemporalAcousticEventKind::Started);
    EXPECT_NE(after[1].occurrence_id, before[0].occurrence_id);
    EXPECT_EQ(after[1].start_time.nanoseconds(), 14);
}

TEST(TemporalAcousticEventDetector, GapDiscouragesJoiningUnconfirmedOnsets) {
    auto opts = audition::TemporalAcousticEventDetectorOptions{};
    opts.max_observation_gap = audition::Duration{2};
    audition::TemporalAcousticEventDetector detector{opts};
    EXPECT_TRUE(detector.observe(source(3U), at(1), scores({{"knock", 0.9}})).empty());
    EXPECT_TRUE(detector.observe(source(3U), at(5), scores({{"knock", 0.9}})).empty());
    const auto start = detector.observe(source(3U), at(6), scores({{"knock", 0.9}}));
    ASSERT_EQ(start.size(), 1U);
    EXPECT_EQ(start[0].start_time.nanoseconds(), 5);
}

TEST(TemporalAcousticEventDetector, RepeatedOccurrencesReceiveDistinctIds) {
    audition::TemporalAcousticEventDetector detector{immediate()};
    const auto first = detector.observe(source(1U), at(1), scores({{"beep", 0.8}}));
    const auto ended = detector.observe(source(1U), at(2), scores({}));
    const auto second = detector.observe(source(1U), at(3), scores({{"beep", 0.9}}));
    ASSERT_EQ(first.size(), 1U);
    ASSERT_EQ(ended.size(), 1U);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_NE(first[0].occurrence_id, second[0].occurrence_id);
    EXPECT_EQ(ended[0].occurrence_id, first[0].occurrence_id);
}

TEST(TemporalAcousticEventDetector, DuplicateAndEmptyLabelsFailWithoutConsumingTime) {
    audition::TemporalAcousticEventDetector detector{immediate()};
    const auto id = source(1U);
    auto started = detector.observe(id, at(10), scores({{"a", 0.8}}));
    ASSERT_EQ(started.size(), 1U);
    EXPECT_THROW(static_cast<void>(detector.observe(id, at(11),
                 scores({{"a", 0.9}, {"a", 0.8}}))), audition::Error);
    EXPECT_THROW(static_cast<void>(detector.observe(id, at(11),
                 scores({{"", 0.9}}))), audition::Error);
    const auto valid = detector.observe(id, at(11), scores({{"a", 0.9}}));
    EXPECT_TRUE(valid.empty());
    const auto active = detector.activeEvents();
    ASSERT_EQ(active.size(), 1U);
    EXPECT_DOUBLE_EQ(active[0].peak_score.value(), 0.9);
}

TEST(TemporalAcousticEventDetector, TimeOrderingAndClockIsolationFailClosed) {
    audition::TemporalAcousticEventDetector detector{immediate()};
    const auto id = source(55U);
    const auto started = detector.observe(id, at(10), scores({{"a", 0.8}}));
    ASSERT_EQ(started.size(), 1U);
    EXPECT_THROW(static_cast<void>(detector.observe(id, at(10), scores({}))), audition::Error);
    EXPECT_THROW(static_cast<void>(detector.observe(id, at(9), scores({}))), audition::Error);
    EXPECT_THROW(static_cast<void>(detector.observe(id, at(11, 99U), scores({}))), audition::Error);
    EXPECT_THROW(static_cast<void>(detector.retire(id, at(11, 99U))), audition::Error);
    EXPECT_EQ(detector.activeEvents().size(), 1U);
    auto finished = detector.retire(id, at(11));
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].last_supported_time.nanoseconds(), 10);
}

TEST(TemporalAcousticEventDetector, LabelCapacityIsTransactional) {
    auto opts = immediate();
    opts.max_labels_per_source = 1U;
    audition::TemporalAcousticEventDetector detector{opts};
    auto first = detector.observe(source(3U), at(1), scores({{"a", 0.8}}));
    ASSERT_EQ(first.size(), 1U);
    EXPECT_THROW(static_cast<void>(detector.observe(source(3U), at(2),
                 scores({{"a", 0.9}, {"b", 0.8}}))), audition::Error);
    auto active = detector.activeEvents();
    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(active[0].last_supported_time.nanoseconds(), 1);
    EXPECT_DOUBLE_EQ(active[0].peak_score.value(), 0.8);
    EXPECT_TRUE(detector.observe(source(3U), at(2),
                scores({{"a", 0.9}})).empty());
    EXPECT_EQ(detector.activeEvents()[0].last_supported_time.nanoseconds(), 2);
    EXPECT_EQ(detector.observe(source(3U), at(3), scores({{"b", 0.8}})).size(), 2U);
}

TEST(TemporalAcousticEventDetector, SourceCapacityIsExplicitAndRecoverable) {
    auto opts = immediate();
    opts.max_sources = 1U;
    audition::TemporalAcousticEventDetector detector{opts};
    EXPECT_TRUE(detector.observe(source(7U), at(1), scores({})).empty());
    EXPECT_THROW(static_cast<void>(detector.observe(source(8U), at(1),
                 scores({{"x", 0.9}}))), audition::Error);
    EXPECT_EQ(detector.sourceCount(), 1U);
    detector.forget(source(7U));
    const auto event = detector.observe(source(8U), at(1), scores({{"x", 0.9}}));
    ASSERT_EQ(event.size(), 1U);
    EXPECT_EQ(event[0].source_id, source(8U));
}

TEST(TemporalAcousticEventDetector, RetireDiscardsCandidateButFinalizesConfirmed) {
    audition::TemporalAcousticEventDetector detector{};
    const auto id = source(5U);
    EXPECT_TRUE(detector.observe(id, at(1),
                scores({{"alarm", 0.8}, {"pending", 0.8}})).empty());
    const auto start = detector.observe(id, at(2), scores({{"alarm", 0.8}}));
    ASSERT_EQ(start.size(), 1U);
    const auto stopped = detector.retire(id, at(5));
    ASSERT_EQ(stopped.size(), 1U);
    EXPECT_EQ(stopped[0].label, "alarm");
    EXPECT_EQ(stopped[0].last_supported_time.nanoseconds(), 2);
    EXPECT_EQ(detector.sourceCount(), 0U);
    EXPECT_TRUE(detector.retire(id, at(7)).empty());
    EXPECT_TRUE(detector.observe(id, at(1), scores({{"alarm", 0.8}})).empty());
}

TEST(TemporalAcousticEventDetector, ResetDropsActiveWithoutReusingOccurrence) {
    audition::TemporalAcousticEventDetector detector{immediate()};
    const auto old = detector.observe(source(2U), at(10), scores({{"bell", 0.8}}));
    ASSERT_EQ(old.size(), 1U);
    detector.reset();
    EXPECT_TRUE(detector.activeEvents().empty());
    EXPECT_EQ(detector.sourceCount(), 0U);
    const auto newer = detector.observe(source(2U), at(1), scores({{"bell", 0.9}}));
    ASSERT_EQ(newer.size(), 1U);
    EXPECT_NE(old[0].occurrence_id, newer[0].occurrence_id);
}

TEST(TemporalAcousticEventDetector, InvalidOptionsAndSourceIdsFailFast) {
    audition::TemporalAcousticEventDetector detector{};
    EXPECT_THROW(static_cast<void>(detector.observe(source(0U), at(1), scores({}))), audition::Error);
    EXPECT_THROW(static_cast<void>(detector.retire(source(0U), at(1))), audition::Error);
    auto invalid = immediate();
    invalid.stop_threshold = audition::Probability::from(0.9);
    EXPECT_THROW(static_cast<void>(audition::TemporalAcousticEventDetector{invalid}),
                 audition::Error);
    invalid = immediate();
    invalid.min_stop_observations = 0U;
    EXPECT_THROW(static_cast<void>(audition::TemporalAcousticEventDetector{invalid}),
                 audition::Error);
    invalid = immediate();
    invalid.max_observation_gap = audition::Duration{-1};
    EXPECT_THROW(static_cast<void>(audition::TemporalAcousticEventDetector{invalid}),
                 audition::Error);
}

TEST(TemporalAcousticEventDetector, TransitionsMapToCallerOwnedAcousticEventAssembler) {
    audition::TemporalAcousticEventDetector detector{};
    audition::AcousticEventAssembler assembler{};
    const auto id = source(17U);
    EXPECT_TRUE(detector.observe(id, at(10), scores({{"alarm", 0.8}})).empty());
    const auto start = detector.observe(id, at(11), scores({{"alarm", 0.85}}));
    ASSERT_EQ(start.size(), 1U);
    const auto acoustic_id = assembler.begin(start[0].start_time);
    audition::AcousticEventPatch patch{};
    patch.source_id = start[0].source_id;
    patch.timestamp = start[0].last_supported_time;
    patch.classification = scores({{"alarm", 0.85}});
    assembler.update(acoustic_id, patch);
    EXPECT_TRUE(detector.observe(id, at(12), scores({{"alarm", 0.2}})).empty());
    const auto ended = detector.observe(id, at(13), scores({{"alarm", 0.1}}));
    ASSERT_EQ(ended.size(), 1U);
    EXPECT_EQ(ended[0].occurrence_id, start[0].occurrence_id);
    const auto completed = assembler.finish(acoustic_id, ended[0].last_supported_time);
    EXPECT_EQ(*completed.source_id, id);
    EXPECT_EQ(completed.start_time.nanoseconds(), 10);
    EXPECT_EQ(completed.end_time.nanoseconds(), 11);
    ASSERT_TRUE(completed.classification.has_value());
    EXPECT_EQ(completed.classification->classes[0].label, "alarm");
}

TEST(TemporalAcousticEventDetector, ManyInterleavedSourcesRemainBounded) {
    auto opts = immediate();
    opts.max_sources = 20U;
    audition::TemporalAcousticEventDetector detector{opts};
    for (std::uint64_t id = 1; id <= 20; ++id) {
        const auto result = detector.observe(source(id), at(100),
                                             scores({{"engine", 0.9}}));
        ASSERT_EQ(result.size(), 1U);
        EXPECT_EQ(result[0].source_id, source(id));
    }
    ASSERT_EQ(detector.activeEvents().size(), 20U);
    for (std::uint64_t id = 1; id <= 20; ++id) {
        const auto result = detector.observe(source(id), at(101), scores({}));
        ASSERT_EQ(result.size(), 1U);
        EXPECT_EQ(result[0].source_id, source(id));
    }
    EXPECT_TRUE(detector.activeEvents().empty());
    EXPECT_EQ(detector.sourceCount(), 20U);
}
