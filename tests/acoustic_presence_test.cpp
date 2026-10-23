#include <audition/dsp/acoustic_presence.hpp>
#include <gtest/gtest.h>
#include <vector>
#include <limits>

TEST(AcousticPresence, ReportsEvidenceWithoutOwningTrackLifecycle) {
    audition::dsp::RmsAcousticPresenceDetector detector{-50.0};
    std::vector<float> quiet(128, 0.0001f);
    std::vector<float> loud(128, 0.02f);
    auto absence = detector.process(quiet.data(), quiet.size(), 16000);
    EXPECT_FALSE(absence.sound_present);
    EXPECT_FALSE(absence.calibrated);
    auto presence = detector.process(loud.data(), loud.size(), 16000);
    EXPECT_TRUE(presence.sound_present);
    EXPECT_FALSE(presence.calibrated);
    detector.reset();
    EXPECT_TRUE(detector.process(loud.data(), loud.size(), 16000).sound_present);
}

TEST(AcousticPresence, InvalidInputIsRejected) {
    audition::dsp::RmsAcousticPresenceDetector detector{};
    EXPECT_THROW(detector.process(nullptr, 1, 16000), std::invalid_argument);
    EXPECT_THROW(detector.process(nullptr, 0, 0), std::invalid_argument);
    std::vector<float> invalid{std::numeric_limits<float>::quiet_NaN()};
    EXPECT_THROW(detector.process(invalid.data(), invalid.size(), 16000),
                 std::invalid_argument);
    EXPECT_THROW((void)audition::dsp::RmsAcousticPresenceDetector{1.0},
                 std::invalid_argument);
}
