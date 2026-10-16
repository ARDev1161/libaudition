#include <audition/audio/recording_timeline_json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <limits>
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


TEST(RecordingTimelineJson, AcceptsOmittedOptionalGapsArray) {
    const auto timeline = audition::recordingTimelineFromJson(
        R"json({"schema":"libaudition.recording-timeline.v1","recording_id":7,"sample_rate_hz":16000,"speech":[],"sounds":[]})json");
    EXPECT_EQ(timeline.recordingId(), 7U);
    EXPECT_EQ(timeline.sampleRateHz(), 16000U);
    EXPECT_TRUE(timeline.gaps().empty());
    EXPECT_TRUE(timeline.speech().empty());
    EXPECT_TRUE(timeline.sounds().empty());
}

TEST(RecordingTimelineJson, IgnoresUnknownTypedFieldsAfterStrictParsing) {
    std::string json = timelineJson();
    json.insert(1,
        R"json("unknown":{"flags":[true,false,null],"nested":{"power":-1.25e+2,"empty":[],"object":{}}},)json");
    const auto timeline = audition::recordingTimelineFromJson(json);
    EXPECT_EQ(timeline.recordingId(), 7U);
    EXPECT_TRUE(timeline.gaps().empty());
    rejected(json + " non-json");
}

TEST(RecordingTimelineJson, RejectsMalformedExtraFieldLiteralsAndDelimiters) {
    for (const std::string_view injected : {
        R"json("extra":tru,)json",
        R"json("extra":nul,)json",
        R"json("extra":fal,)json",
        R"json("extra":[1 2],)json",
        R"json("extra":[,],)json",
        R"json("extra":{"a":1 "b":2},)json",
        R"json("extra":{"a":1,},)json",
        R"json("extra":--1,)json",
        R"json("extra":1e-,)json",
        R"json("extra":.5,)json",
    }) {
        std::string json = timelineJson();
        json.insert(1, injected);
        rejected(json);
    }
}

TEST(RecordingTimelineJson, AcceptsAllStandardStringEscapes) {
    const auto speech = R"json([{"start_sample":0,"end_sample":1,"text":"a\"b\\c\/d\b\f\n\r\t","language":"en"}])json";
    const auto timeline = audition::recordingTimelineFromJson(timelineJson(speech));
    ASSERT_EQ(timeline.speech().size(), 1U);
    EXPECT_EQ(timeline.speech()[0].text,
              std::string{"a\"b\\c/d\b\f\n\r\t"});
}

TEST(RecordingTimelineJson, RejectsTruncatedAndInvalidStringEscapes) {
    rejected(R"json({"schema":"broken\q"})json");
    rejected(R"json({"schema":"broken\u12"})json");
    rejected(R"json({"schema":"broken\u12xz"})json");
    rejected(R"json({"schema":"broken\")json");
    rejected(R"json({"schema":"broken)json");
    std::string json = timelineJson();
    json.insert(1, "\"extra\":\"line\n\",");
    rejected(json);
}

TEST(RecordingTimelineJson, DecodesAsciiBmpAndSupplementaryUnicodeEscapes) {
    const auto speech = R"json([{"start_sample":0,"end_sample":1,"text":"\u0041 \u20ac \ud83d\ude80","language":"en"}])json";
    const auto timeline = audition::recordingTimelineFromJson(timelineJson(speech));
    ASSERT_EQ(timeline.speech().size(), 1U);
    EXPECT_EQ(timeline.speech()[0].text, "A \xe2\x82\xac \xf0\x9f\x9a\x80");
}

TEST(RecordingTimelineJson, RejectsBadSurrogatePairBoundaryValues) {
    const auto make = [](std::string_view escaped) {
        return timelineJson(
            std::string{R"json([{"start_sample":0,"end_sample":1,"text":")json"} +
            std::string{escaped} +
            R"json(","language":"en"}])json");
    };
    rejected(make(R"json(\ud800\u0000)json"));
    rejected(make(R"json(\ud800\udbff)json"));
    rejected(make(R"json(\ud800\ue000)json"));
    rejected(make(R"json(\udfff)json"));
    rejected(make(R"json(\ud800\udc)json"));
}

TEST(RecordingTimelineJson, AcceptsInclusiveProbabilityEndpoints) {
    const auto sounds = R"json([{"start_sample":0,"end_sample":1,"label":"quiet","model":"m","confidence":0,"stage":"quick"},{"start_sample":1,"end_sample":2,"label":"alarm","model":"m","confidence":1,"stage":"refined"}])json";
    const auto timeline = audition::recordingTimelineFromJson(
        timelineJson("[]", sounds));
    ASSERT_EQ(timeline.sounds().size(), 2U);
    EXPECT_DOUBLE_EQ(timeline.sounds()[0].confidence, 0.0);
    EXPECT_DOUBLE_EQ(timeline.sounds()[1].confidence, 1.0);
    EXPECT_EQ(timeline.sounds()[1].stage, "refined");
}

TEST(RecordingTimelineJson, RejectsMalformedProbabilityTypesAndNumbers) {
    const auto make = [](std::string_view number) {
        return timelineJson("[]",
            std::string{R"json([{"start_sample":0,"end_sample":1,"label":"noise","model":"m","confidence":)json"} +
            std::string{number} + R"json(,"stage":"quick"}])json");
    };
    for (const std::string_view invalid : {
         "null", "true", "\"0.5\"", "-0.001", "1.001", "1e999"}) {
        rejected(make(invalid));
    }
}

TEST(RecordingTimelineJson, RejectsFractionalAndOutOfRangeEventOffsets) {
    const auto make = [](std::string_view offset) {
        return timelineJson(
            std::string{R"json([{"start_sample":)json"} +
            std::string{offset} +
            R"json(,"end_sample":5,"text":"hi","language":"en"}])json");
    };
    for (const std::string_view invalid : {
        "-1", "0.5", "1e0", "18446744073709551616", "null", "true"}) {
        rejected(make(invalid));
    }
}

TEST(RecordingTimelineJson, ZeroLengthGapsAreIgnoredAndDifferentReasonsDoNotMerge) {
    const auto gaps = R"json([{"start_sample":10,"end_sample":10,"reason":"empty"},{"start_sample":10,"end_sample":20,"reason":"overflow"},{"start_sample":20,"end_sample":30,"reason":"other"},{"start_sample":30,"end_sample":40,"reason":"other"}])json";
    const auto timeline = audition::recordingTimelineFromJson(
        timelineJson("[]", "[]", gaps));
    ASSERT_EQ(timeline.gaps().size(), 2U);
    EXPECT_EQ(timeline.gaps()[0].start_sample, 10U);
    EXPECT_EQ(timeline.gaps()[0].end_sample, 20U);
    EXPECT_EQ(timeline.gaps()[0].reason, "overflow");
    EXPECT_EQ(timeline.gaps()[1].start_sample, 20U);
    EXPECT_EQ(timeline.gaps()[1].end_sample, 40U);
    EXPECT_EQ(timeline.gaps()[1].reason, "other");
}

TEST(RecordingTimelineJson, FileSidecarWriteAndReadRoundTripsEscapedText) {
    const auto path = std::filesystem::temp_directory_path() /
        "libaudition-timeline-json-write-read.json";
    audition::RecordingTimeline input{987U, 48000U};
    input.add(audition::TimelineSpeechEvent{
        987U, 10U, 20U, "quoted \"text\"\nnext line", "ru"});
    input.add(audition::TimelineSoundEvent{
        987U, 20U, 25U, "alarm", "yamnet", 0.75, "refined"});
    audition::writeRecordingTimelineJson(path, input);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    const auto output = audition::readRecordingTimelineJson(path);
    EXPECT_EQ(output.recordingId(), 987U);
    EXPECT_EQ(output.sampleRateHz(), 48000U);
    ASSERT_EQ(output.speech().size(), 1U);
    EXPECT_EQ(output.speech()[0].text, input.speech()[0].text);
    ASSERT_EQ(output.sounds().size(), 1U);
    EXPECT_DOUBLE_EQ(output.sounds()[0].confidence, 0.75);
    std::filesystem::remove(path);
}

TEST(RecordingTimelineJson, AcceptsMaxUint64RecordingIdentity) {
    const auto json =
        R"json({"schema":"libaudition.recording-timeline.v1","recording_id":18446744073709551615,"sample_rate_hz":4294967295,"speech":[],"sounds":[],"gaps":[]})json";
    const auto timeline = audition::recordingTimelineFromJson(json);
    EXPECT_EQ(timeline.recordingId(),
              std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(timeline.sampleRateHz(),
              std::numeric_limits<std::uint32_t>::max());
}
