#include <audition/interfaces/speaker.hpp>

#include <optional>

#include <gtest/gtest.h>

TEST(SpeakerContracts, DiarizationUsesLocalClusterLabels) {
    audition::SpeakerDiarizationResult result{};
    result.speaker_count = 2U;
    result.segments.push_back({
        1U,
        audition::Duration{100},
        audition::Duration{200},
        audition::Score{0.7},
    });

    ASSERT_EQ(result.segments.size(), 1U);
    EXPECT_EQ(result.segments.front().speaker_index, 1U);
    EXPECT_EQ(result.speaker_count, 2U);
    // speaker_index is a local diarization cluster label, not a persistent SpeakerId.
}

TEST(SpeakerContracts, EnrollmentIsSeparateFromPersistentStorage) {
    audition::SpeakerEnrollment enrollment{};
    enrollment.speaker_id = audition::SpeakerId{42};
    enrollment.display_name = "speaker";
    enrollment.embeddings.push_back({"model", {0.1F, 0.2F}, std::nullopt});

    EXPECT_TRUE(enrollment.speaker_id.valid());
    EXPECT_EQ(enrollment.embeddings.size(), 1U);
}
