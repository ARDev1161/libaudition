#include <audition/interfaces/speech.hpp>

#include <gtest/gtest.h>

TEST(SpeechContracts, MissingBackendScoresAreRepresentable) {
    audition::VadResult vad{};
    EXPECT_FALSE(vad.speech_probability.has_value());

    audition::KeywordHit hit{};
    EXPECT_FALSE(hit.probability.has_value());

    audition::LanguageScore language{};
    EXPECT_FALSE(language.probability.has_value());
}

TEST(SpeechContracts, TimedTokenMayHaveOnlyStartOffset) {
    audition::TimedToken token{};
    token.text = "hello";
    token.start_offset = audition::Duration{123};
    EXPECT_FALSE(token.end_offset.has_value());

    audition::Transcript transcript{};
    transcript.tokens.push_back(token);
    ASSERT_EQ(transcript.tokens.size(), 1U);
    EXPECT_EQ(transcript.tokens.front().start_offset.nanoseconds(), 123);
}
