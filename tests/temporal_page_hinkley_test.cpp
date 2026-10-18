#include <audition/temporal/page_hinkley.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

namespace {
audition::Timestamp tick(int t, unsigned clock = 1) {
    return {t, {audition::ClockDomain::Simulation, clock}};
}
}

TEST(PageHinkleyDetector, DetectsUpwardSustainedShift) {
    audition::temporal::PageHinkleyOptions o{};
    o.threshold = 1.0;
    o.allowance = 0;
    audition::temporal::PageHinkleyDetector d(o);
    for (int i = 1; i <= 5; ++i) {
        EXPECT_FALSE(d.observe(audition::AcousticSourceId{1}, "energy", tick(i), 0).upward_change);
    }
    bool detected = false;
    for (int i = 6; i <= 10; ++i)
        detected |= d.observe(audition::AcousticSourceId{1}, "energy", tick(i), 2).upward_change;
    EXPECT_TRUE(detected);
}

TEST(PageHinkleyDetector, DetectsDownwardSustainedShift) {
    audition::temporal::PageHinkleyOptions o{};
    o.threshold = 1;
    o.allowance = 0;
    audition::temporal::PageHinkleyDetector d(o);
    for (int i = 1; i <= 5; ++i)
        (void)d.observe(audition::AcousticSourceId{1}, "energy", tick(i), 3);
    bool detected = false;
    for (int i = 6; i <= 10; ++i)
        detected |= d.observe(audition::AcousticSourceId{1}, "energy", tick(i), 0).downward_change;
    EXPECT_TRUE(detected);
}

TEST(PageHinkleyDetector, IndependentSourcesAndClockValidation) {
    audition::temporal::PageHinkleyDetector d;
    (void)d.observe(audition::AcousticSourceId{1}, "x", tick(10), 0);
    (void)d.observe(audition::AcousticSourceId{2}, "x", tick(10, 2), 5);
    EXPECT_THROW(static_cast<void>(d.observe(audition::AcousticSourceId{1}, "x", tick(11,2), 1)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(d.observe(audition::AcousticSourceId{1}, "x", tick(10), 1)), std::invalid_argument);
    EXPECT_EQ(d.observe(audition::AcousticSourceId{1}, "x", tick(11), 0).observations, 2u);
}

TEST(PageHinkleyDetector, CapacityAndReset) {
    audition::temporal::PageHinkleyOptions o{};
    o.max_sources = 1;
    o.max_features_per_source = 1;
    audition::temporal::PageHinkleyDetector d(o);
    (void)d.observe(audition::AcousticSourceId{1}, "x", tick(1), 0);
    EXPECT_THROW(static_cast<void>(d.observe(audition::AcousticSourceId{2}, "x", tick(1), 0)), std::length_error);
    EXPECT_THROW(static_cast<void>(d.observe(audition::AcousticSourceId{1}, "y", tick(2), 0)), std::length_error);
    EXPECT_THROW(static_cast<void>(d.observe(audition::AcousticSourceId{1}, "x", tick(2), std::numeric_limits<double>::infinity())), std::invalid_argument);
    d.reset();
    EXPECT_EQ(d.observe(audition::AcousticSourceId{2}, "x", tick(1), 4).observations, 1u);
}

TEST(PageHinkleyDetector, OptionsValidation) {
    audition::temporal::PageHinkleyOptions o{};
    o.threshold = 0;
    EXPECT_THROW(audition::temporal::PageHinkleyDetector{o}, std::invalid_argument);
}
