#include <audition/speech/per_source_speech_runtime.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <atomic>
#include <memory>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace audition;
namespace {
class ThresholdSession : public IVadSession {
public:
    void reset() override {}
    VadResult process(AudioView audio) override {
        bool active = false;
        for (std::size_t i=0; i<audio.sampleCount(); ++i)
            if (audio.data()[i] > 0.2F) active = true;
        return {std::nullopt, active};
    }
};
class ThresholdVad : public IVoiceActivityDetector {
public:
    BackendInfo backendInfo() const override {return {"fake-vad","1"};}
    AudioRequirements audioRequirements() const override {return {};}
    std::unique_ptr<IVadSession> createSession() const override {
        return std::make_unique<ThresholdSession>();
    }
};
class MockAsr : public IAsrEngine {
public:
    BackendInfo backendInfo() const override {return {"fake-asr","1"};}
    AsrCapabilities capabilities() const override {return {};}
    Transcript transcribe(const SpeechSegment& segment) const override {
        Transcript t{};
        t.segment_id = segment.segment_id;
        t.text = std::to_string(segment.track_id->value()) + ":" +
                 std::to_string(segment.audio.samples().size());
        return t;
    }
};
AudioView chunk(const std::vector<float>& values) {
    return AudioView{values.data(),values.size(),
        {16000U,1U,AudioLayout::Interleaved},Timestamp{}};
}
}
TEST(PerSourceSpeechRuntime, IndependentlySegmentsTwoTracksAndRunsAsyncAsr) {
    ThresholdVad vad;
    auto asr=std::make_shared<MockAsr>();
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples=4;
    opts.pre_roll_samples=0;
    opts.post_roll_samples=0;
    PerSourceSpeechRuntime runtime{vad,asr,opts};
    const std::vector<float> speech(4,0.5F), silence(4,0.0F);
    runtime.push(17U,chunk(speech));
    runtime.push(19U,chunk(speech));
    runtime.push(17U,chunk(silence));
    runtime.push(19U,chunk(silence));
    std::vector<SourceSpeechResult> found;
    for(int i=0;i<1000 && found.size()<2U;++i){
        auto results=runtime.takeResults();
        found.insert(found.end(),std::make_move_iterator(results.begin()),
                     std::make_move_iterator(results.end()));
        if(found.size()<2U)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(found.size(),2U);
    EXPECT_EQ(found[0].track_id,17U);
    EXPECT_EQ(found[0].transcript.text,"17:4");
    EXPECT_EQ(found[1].track_id,19U);
    EXPECT_EQ(found[1].transcript.text,"19:4");
    EXPECT_EQ(runtime.statistics().completed_segments,2U);
}
TEST(PerSourceSpeechRuntime, ForgetAndResetDiscardUnfinishedAudio) {
    ThresholdVad vad;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>()};
    const std::vector<float> sound(400,0.5F), silence(200,0.F);
    runtime.push(3,chunk(sound));
    runtime.forget(3);
    runtime.push(3,chunk(silence));
    runtime.reset();
    EXPECT_TRUE(runtime.takeResults().empty());
    EXPECT_EQ(runtime.statistics().completed_segments,0U);
}
TEST(PerSourceSpeechRuntime, RejectsIncorrectAudioRate) {
    ThresholdVad vad;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>()};
    const std::vector<float> values(4,.5F);
    AudioView view{values.data(),values.size(),
        {8000U,1U,AudioLayout::Interleaved},Timestamp{}};
    EXPECT_THROW(runtime.push(0,view),std::invalid_argument);
}

TEST(PerSourceSpeechRuntime, PreservesPreRollAndWaitsForPostRoll) {
    ThresholdVad vad;
    PerSourceSpeechOptions cfg{};
    cfg.min_speech_samples = 4;
    cfg.pre_roll_samples = 4;
    cfg.post_roll_samples = 4;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>(),cfg};
    const std::vector<float> silence(2,0.0F), voice(4,0.5F);
    runtime.push(9,chunk(silence));
    runtime.push(9,chunk(silence));
    runtime.push(9,chunk(voice));
    runtime.push(9,chunk(silence));
    EXPECT_TRUE(runtime.takeResults().empty());
    runtime.push(9,chunk(silence));
    bool received = false;
    for(int i=0;i<1000;++i){
        auto found=runtime.takeResults();
        if(!found.empty()){
            ASSERT_EQ(found.size(),1U);
            EXPECT_EQ(found[0].transcript.text,"9:12");
            received=true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(received);
}

TEST(PerSourceSpeechRuntime, SilencePaddingDoesNotSatisfySpeechMinimum) {
    ThresholdVad vad;
    PerSourceSpeechOptions cfg{};
    cfg.min_speech_samples = 4;
    cfg.pre_roll_samples = 20;
    cfg.post_roll_samples = 20;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>(),cfg};
    const std::vector<float> silence(20,0.0F), tiny_voice(1,0.5F);
    runtime.push(41U,chunk(silence));
    runtime.push(41U,chunk(tiny_voice));
    runtime.push(41U,chunk(silence));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(runtime.takeResults().empty());
    EXPECT_EQ(runtime.statistics().completed_segments,0U);
}

namespace {
class ThrowOnNegativeVadSession final : public IVadSession {
public:
    void reset() override {}
    VadResult process(AudioView audio) override {
        if (audio.data()[0] < 0.0F) throw std::runtime_error{"Failed VAD source"};
        return {std::nullopt, audio.data()[0] > 0.2F};
    }
};
class ThrowOnNegativeVad final : public IVoiceActivityDetector {
public:
    BackendInfo backendInfo() const override {return {"throwing-vad", "1"};}
    AudioRequirements audioRequirements() const override {return {};}
    std::unique_ptr<IVadSession> createSession() const override {
        return std::make_unique<ThrowOnNegativeVadSession>();
    }
};
class TimestampAsr final : public IAsrEngine {
public:
    BackendInfo backendInfo() const override {return {"timestamp-asr", "1"};}
    AsrCapabilities capabilities() const override {return {};}
    Transcript transcribe(const SpeechSegment& segment) const override {
        Transcript result{};
        result.segment_id = segment.segment_id;
        result.text = std::to_string(segment.audio.captureTime().nanoseconds()) + ":" +
                      std::to_string(segment.audio.samples().size());
        return result;
    }
};
std::vector<SourceSpeechResult> awaitSpeech(PerSourceSpeechRuntime& runtime,
                                           std::size_t expected) {
    std::vector<SourceSpeechResult> collected;
    for (int retry = 0; retry < 1500 && collected.size() < expected; ++retry) {
        auto chunk = runtime.takeResults();
        collected.insert(collected.end(), std::make_move_iterator(chunk.begin()),
                         std::make_move_iterator(chunk.end()));
        if (collected.size() < expected)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return collected;
}
}

TEST(PerSourceSpeechRuntime, ContinuousSpeechCrossesSegmentCapWithoutDroppingSamples) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 1U;
    opts.pre_roll_samples = 0U;
    opts.post_roll_samples = 0U;
    opts.max_segment_samples = 5U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), opts};
    const std::vector<float> speech(12U, 0.5F), silence(2U, 0.0F);
    runtime.push(5U, chunk(speech));
    runtime.push(5U, chunk(silence));
    const auto received = awaitSpeech(runtime, 3U);
    ASSERT_EQ(received.size(), 3U);
    EXPECT_EQ(received[0].transcript.text, "5:5");
    EXPECT_EQ(received[1].transcript.text, "5:5");
    EXPECT_EQ(received[2].transcript.text, "5:2");
    EXPECT_EQ(runtime.statistics().dropped_segments, 0U);
}

TEST(PerSourceSpeechRuntime, PostRollUsesExactRequestedSampleCount) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 1U;
    opts.pre_roll_samples = 2U;
    opts.post_roll_samples = 3U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), opts};
    const std::vector<float> pre(2U, 0.0F), voiced(4U, 0.5F), trailing(8U, 0.0F);
    runtime.push(1U, chunk(pre));
    runtime.push(1U, chunk(voiced));
    runtime.push(1U, chunk(trailing));
    const auto received = awaitSpeech(runtime, 1U);
    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received.front().transcript.text, "1:9"); // 2 + 4 + 3
    EXPECT_EQ(runtime.statistics().dropped_segments, 0U);
}

TEST(PerSourceSpeechRuntime, VADFailureQuarantinesTrackButOtherSourceContinues) {
    ThrowOnNegativeVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 4U;
    opts.pre_roll_samples = 0U;
    opts.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), opts};
    const std::vector<float> wrong(4U, -0.5F), voiced(4U, 0.5F), silence(4U, 0.0F);
    EXPECT_NO_THROW(runtime.push(20U, chunk(wrong)));
    const auto error = runtime.trackStatus(20U);
    ASSERT_TRUE(error.exists);
    ASSERT_TRUE(error.error.has_value());
    EXPECT_EQ(*error.error, "Failed VAD source");
    EXPECT_NO_THROW(runtime.push(21U, chunk(voiced)));
    runtime.push(21U, chunk(silence));
    const auto received = awaitSpeech(runtime, 1U);
    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received.front().track_id, 21U);
    EXPECT_EQ(received.front().transcript.text, "21:4");
    runtime.forget(20U);
    EXPECT_FALSE(runtime.trackStatus(20U).exists);
    runtime.push(20U, chunk(voiced));
    runtime.push(20U, chunk(silence));
    const auto recovered = awaitSpeech(runtime, 1U);
    ASSERT_EQ(recovered.size(), 1U);
    EXPECT_EQ(recovered.front().transcript.text, "20:4");
}

TEST(PerSourceSpeechRuntime, NonfiniteTrackPCMDoesNotStopOtherSources) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 4U;
    opts.pre_roll_samples = 0U;
    opts.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), opts};
    const std::vector<float> bad(4U, std::numeric_limits<float>::quiet_NaN());
    const std::vector<float> voice(4U, 0.5F), quiet(4U, 0.0F);
    EXPECT_NO_THROW(runtime.push(7U, chunk(bad)));
    EXPECT_TRUE(runtime.trackStatus(7U).error.has_value());
    runtime.push(8U, chunk(voice));
    runtime.push(8U, chunk(quiet));
    const auto recovered = awaitSpeech(runtime, 1U);
    ASSERT_EQ(recovered.size(), 1U);
    EXPECT_EQ(recovered[0].track_id, 8U);
}

TEST(PerSourceSpeechRuntime, ContinuousSegmentTimestampAdvancesWithAudioClock) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 1U;
    opts.pre_roll_samples = 0U;
    opts.post_roll_samples = 0U;
    opts.max_segment_samples = 5U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<TimestampAsr>(), opts};
    const std::vector<float> samples(12U, 0.5F), silence(2U, 0.0F);
    const auto view = AudioView{samples.data(), samples.size(),
        {16000U, 1U, AudioLayout::Interleaved},
        Timestamp{1000000000LL, {ClockDomain::Monotonic, 1U}}};
    runtime.push(1U, view);
    runtime.push(1U, chunk(silence));
    const auto received = awaitSpeech(runtime, 3U);
    ASSERT_EQ(received.size(), 3U);
    EXPECT_EQ(received[0].transcript.text, "1000000000:5");
    EXPECT_EQ(received[1].transcript.text, "1000312500:5");
    EXPECT_EQ(received[2].transcript.text, "1000625000:2");
}

namespace {
class SlowAsr final : public IAsrEngine {
public:
    mutable std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    mutable std::atomic<int> calls{0};
    BackendInfo backendInfo() const override {return {"gated-asr", "1"};}
    AsrCapabilities capabilities() const override {return {};}
    Transcript transcribe(const SpeechSegment& segment) const override {
        const int call = ++calls;
        if (call == 1) {
            entered.store(true);
            while (!release.load())
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        Transcript result{};
        result.segment_id = segment.segment_id;
        result.text = std::to_string(call);
        return result;
    }
};
class FailFirstSourceAsr final : public IAsrEngine {
public:
    BackendInfo backendInfo() const override {return {"failing-asr", "1"};}
    AsrCapabilities capabilities() const override {return {};}
    Transcript transcribe(const SpeechSegment& segment) const override {
        if (segment.track_id->value() == 100U)
            throw std::runtime_error{"Source 100 ASR failed"};
        Transcript result{};
        result.segment_id = segment.segment_id;
        result.text = "source-" + std::to_string(segment.track_id->value());
        return result;
    }
};
}

TEST(PerSourceSpeechRuntime, RetiredInFlightAsrCannotPublishUnderReusedTrackId) {
    ThresholdVad vad;
    auto gated = std::make_shared<SlowAsr>();
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, gated, options};
    const std::vector<float> speech(4U, 0.5F), silence(4U, 0.0F);
    runtime.push(60U, chunk(speech));
    runtime.push(60U, chunk(silence));
    for (int i = 0; i < 1000 && !gated->entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gated->entered.load()) {
        gated->release.store(true);
        FAIL() << "Initial ASR did not start";
        return;
    }
    runtime.forget(60U);
    runtime.push(60U, chunk(speech));
    runtime.push(60U, chunk(silence));
    gated->release.store(true);
    const auto result = awaitSpeech(runtime, 1U);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0].track_id, 60U);
    EXPECT_EQ(result[0].transcript.text, "2");
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
    EXPECT_TRUE(runtime.takeResults().empty());
}

TEST(PerSourceSpeechRuntime, AsrExceptionDoesNotStopOtherSources) {
    ThresholdVad vad;
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<FailFirstSourceAsr>(), options};
    const std::vector<float> voice(4U, 0.5F), silence(4U, 0.0F);
    for (const std::uint64_t id : {100U, 101U}) {
        runtime.push(id, chunk(voice));
        runtime.push(id, chunk(silence));
    }
    const auto result = awaitSpeech(runtime, 1U);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0].track_id, 101U);
    EXPECT_EQ(result[0].transcript.text, "source-101");
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
    EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
    ASSERT_TRUE(runtime.statistics().last_error.has_value());
    EXPECT_EQ(*runtime.statistics().last_error, "Source 100 ASR failed");
}


TEST(PerSourceSpeechRuntime, RetireFlushesLastSpeechAndPreservesReusedIdGeneration) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 4;
    opts.pre_roll_samples = 0;
    opts.post_roll_samples = 16;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), opts};
    const std::vector<float> voice(12U, 0.5F);
    runtime.push(3U, chunk(voice));
    const auto first = runtime.retire(3U);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(runtime.trackStatus(3U).exists);
    runtime.push(3U, chunk(voice));
    const auto second = runtime.retire(3U);
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    const auto result = awaitSpeech(runtime, 2U);
    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0].generation, *first);
    EXPECT_EQ(result[1].generation, *second);
    EXPECT_EQ(result[0].transcript.text, "3:12");
    EXPECT_EQ(result[1].transcript.text, "3:12");
    EXPECT_EQ(runtime.pendingResults(*first), 0U);
    EXPECT_EQ(runtime.pendingResults(*second), 0U);
    EXPECT_NO_THROW(runtime.releaseRetired(*first));
    EXPECT_NO_THROW(runtime.releaseRetired(*second));
}


TEST(PerSourceSpeechRuntime, RetiredAsrSurvivesCancellationOfReusedId) {
    ThresholdVad vad;
    auto gated = std::make_shared<SlowAsr>();
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, gated, options};
    const std::vector<float> voice(4U, 0.5F), silence(4U, 0.0F);
    runtime.push(60U, chunk(voice));
    runtime.push(60U, chunk(silence));
    for (int i = 0; i < 1000 && !gated->entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gated->entered.load()) {
        gated->release.store(true);
        FAIL() << "Initial ASR did not start";
        return;
    }
    const auto originalGeneration = runtime.retire(60U);
    ASSERT_TRUE(originalGeneration.has_value());
    runtime.push(60U, chunk(voice));
    runtime.push(60U, chunk(silence));
    const auto reusedGeneration = runtime.activeGeneration(60U);
    ASSERT_TRUE(reusedGeneration.has_value());
    EXPECT_NE(*originalGeneration, *reusedGeneration);
    runtime.forget(60U);
    EXPECT_FALSE(runtime.activeGeneration(60U).has_value());
    EXPECT_EQ(runtime.pendingResults(*originalGeneration), 1U);
    gated->release.store(true);
    audition::SourceSpeechDrain drained;
    for (int i = 0; i < 1000; ++i) {
        drained = runtime.drainReady();
        if (!drained.ready_retired_generations.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(drained.ready_retired_generations.size(), 1U);
    EXPECT_EQ(drained.ready_retired_generations.front(), *originalGeneration);
    ASSERT_EQ(drained.results.size(), 1U);
    EXPECT_EQ(drained.results.front().generation, *originalGeneration);
    EXPECT_EQ(drained.results.front().transcript.text, "1");
    EXPECT_NO_THROW(runtime.releaseRetired(*originalGeneration));
    EXPECT_TRUE(runtime.drainReady().ready_retired_generations.empty());
}
