#include <audition/audio/recording_timeline_json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::string timelineJson(std::string speech = "[]", std::string sounds = "[]",
                         std::string gaps = "[]") {
    return "{\"schema\":\"libaudition.recording-timeline.v1\","
           "\"recording_id\":7,\"sample_rate_hz\":16000,\"speech\":" +
        speech + ",\"sounds\":" + sounds + ",\"gaps\":" + gaps + "}";
}
void rejected(std::string_view json) {
    EXPECT_THROW(static_cast<void>(audition::recordingTimelineFromJson(json)),
                 std::invalid_argument) << json;
}
} // namespace

TEST(RecordingTimelineJson, RejectsMalformedSyntaxAndTrailingContent) {
    for (const std::string_view json : {
        "", "{", "[]", "null", "true", "garbage", "{}", "{\"a\":1,\"a\":2}",
        "{\"a\":}", "{\"a\":1,}", "[1,]", "{\"a\" 1}",
        "{\"a\":01}", "{\"a\":-}", "{\"a\":1.}",
        "{\"a\":1e}", "{\"a\":1e+}", "{\"a\":\"\\x\"}",
        "{\"a\":\"raw\nnewline\"}", "{\"a\":\"unterminated}",
    }) rejected(json);
    rejected(timelineJson() + " trailing");
    rejected(timelineJson("{}", "[]"));
    rejected(timelineJson("[]", "{}", "[]"));
}

TEST(RecordingTimelineJson, RejectsInvalidMetadataAndFieldTypes) {
    const auto valid = timelineJson();
    auto changed = [&](std::string_view from, std::string_view to) {
        std::string data = valid;
        const auto pos = data.find(from);
        ASSERT_NE(pos, std::string::npos);
        data.replace(pos, from.size(), to);
        rejected(data);
    };
    changed("libaudition.recording-timeline.v1", "v2");
    changed("\"recording_id\":7", "\"recording_id\":0");
    changed("\"recording_id\":7", "\"recording_id\":-1");
    changed("\"recording_id\":7", "\"recording_id\":7.5");
    changed("\"recording_id\":7", "\"recording_id\":1e2");
    changed("\"recording_id\":7", "\"recording_id\":18446744073709551616");
    changed("\"sample_rate_hz\":16000", "\"sample_rate_hz\":0");
    changed("\"sample_rate_hz\":16000", "\"sample_rate_hz\":4294967296");
    changed("\"speech\":[]", "\"speech\":null");
    changed("\"sounds\":[]", "\"sounds\":false");
    changed("\"gaps\":[]", "\"gaps\":{}");
}

TEST(RecordingTimelineJson, ValidatesEventBoundsAndProbabilities) {
    rejected(timelineJson("[{\"start_sample\":3,\"end_sample\":2,"
                          "\"text\":\"hi\",\"language\":\"en\"}]"));
    rejected(timelineJson("[]", "[{\"start_sample\":0,\"end_sample\":3,"
                          "\"label\":\"noise\",\"model\":\"test\","
                          "\"confidence\":1.2,\"stage\":\"quick\"}]"));
    rejected(timelineJson("[]", "[{\"start_sample\":0,\"end_sample\":3,"
                          "\"label\":\"noise\",\"model\":\"test\","
                          "\"confidence\":-0.1,\"stage\":\"quick\"}]"));
    rejected(timelineJson("[]", "[{\"start_sample\":0,\"end_sample\":3,"
                          "\"label\":\"noise\",\"model\":\"test\","
                          "\"confidence\":1e999,\"stage\":\"quick\"}]"));
    rejected(timelineJson("[{\"start_sample\":0,\"end_sample\":1,"
                          "\"text\":null,\"language\":\"en\"}]"));
    rejected(timelineJson("[]", "[]",
                          "[{\"start_sample\":10,\"end_sample\":9,"
                          "\"reason\":\"drop\"}]"));
}

TEST(RecordingTimelineJson, DecodesUnicodeAndMergesAdjacentGaps) {
    const auto json = timelineJson(
        "[{\"start_sample\":0,\"end_sample\":10,"
        "\"text\":\"\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442 \\ud83d\\ude00\","
        "\"language\":\"ru\"}]",
        "[]",
        "[{\"start_sample\":10,\"end_sample\":20,\"reason\":\"lost\"},"
        "{\"start_sample\":20,\"end_sample\":30,\"reason\":\"lost\"}]");
    const auto timeline = audition::recordingTimelineFromJson(json);
    ASSERT_EQ(timeline.speech().size(), 1U);
    EXPECT_EQ(timeline.speech()[0].text,
              "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xf0\x9f\x98\x80");
    ASSERT_EQ(timeline.gaps().size(), 1U);
    EXPECT_EQ(timeline.gaps()[0].start_sample, 10U);
    EXPECT_EQ(timeline.gaps()[0].end_sample, 30U);
    const auto roundtrip = audition::recordingTimelineFromJson(
        audition::recordingTimelineToJson(timeline));
    ASSERT_EQ(roundtrip.speech().size(), 1U);
    EXPECT_EQ(roundtrip.speech()[0].text, timeline.speech()[0].text);
}

TEST(RecordingTimelineJson, RejectsInvalidUnicodeSurrogates) {
    auto speech = [](const char* escaped) {
        return timelineJson(std::string{
            "[{\"start_sample\":0,\"end_sample\":1,\"text\":\""} +
            escaped + "\",\"language\":\"en\"}]");
    };
    rejected(speech("\\ud800"));
    rejected(speech("\\udc00"));
    rejected(speech("\\ud800\\u0041"));
    rejected(speech("\\u12xz"));
    rejected(speech("\\ud800\\u"));
}

TEST(RecordingTimelineJson, EnforcesNestingAndInputSizeLimits) {
    std::string nested(50, '[');
    nested += "0";
    nested.append(50, ']');
    rejected(nested);
    EXPECT_THROW(static_cast<void>(audition::recordingTimelineFromJson(
        std::string(64U * 1024U * 1024U + 1U, ' '))), std::invalid_argument);
}

TEST(RecordingTimelineJson, ReadsSidecarFromFilesystem) {
    const auto path = std::filesystem::temp_directory_path() /
        "libaudition-timeline-json-unit-test.json";
    {
        std::ofstream out(path);
        out << timelineJson();
    }
    const auto loaded = audition::readRecordingTimelineJson(path);
    EXPECT_EQ(loaded.recordingId(), 7U);
    EXPECT_EQ(loaded.sampleRateHz(), 16000U);
    std::filesystem::remove(path);
    EXPECT_THROW(static_cast<void>(audition::readRecordingTimelineJson(path)),
                 std::filesystem::filesystem_error);
}
