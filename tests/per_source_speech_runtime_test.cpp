#include <audition/speech/per_source_speech_runtime.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <memory>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
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
TEST(PerSourceSpeechRuntime, CountersAreSourceLocalAndResetOnReuse) {
    ThresholdVad vad;
    PerSourceSpeechOptions options{};
    options.min_speech_samples=4U;
    options.pre_roll_samples=0U;
    options.post_roll_samples=0U;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>(),options};
    const std::vector<float> voice(4U,0.5F), quiet(4U,0.0F);
    runtime.push(41U,chunk(voice));
    runtime.push(42U,chunk(quiet));
    EXPECT_EQ(runtime.trackStatus(41U).received_samples,4U);
    EXPECT_EQ(runtime.trackStatus(42U).received_samples,4U);
    runtime.push(41U,chunk(quiet));
    EXPECT_EQ(runtime.trackStatus(41U).received_samples,8U);
    EXPECT_EQ(runtime.trackStatus(41U).queued_segments,1U);
    EXPECT_EQ(runtime.trackStatus(41U).dropped_segments,0U);
    EXPECT_EQ(runtime.trackStatus(42U).queued_segments,0U);
    runtime.forget(41U);
    EXPECT_FALSE(runtime.trackStatus(41U).exists);
    runtime.push(41U,chunk(voice));
    EXPECT_EQ(runtime.trackStatus(41U).received_samples,4U);
    EXPECT_EQ(runtime.trackStatus(41U).queued_segments,0U);
    EXPECT_EQ(runtime.trackStatus(42U).received_samples,4U);
    runtime.reset();
    EXPECT_FALSE(runtime.trackStatus(41U).exists);
    EXPECT_FALSE(runtime.trackStatus(42U).exists);
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
    if (!originalGeneration) {
        gated->release.store(true);
        FAIL() << "Retirement lost the original generation";
        return;
    }
    runtime.push(60U, chunk(voice));
    runtime.push(60U, chunk(silence));
    const auto reusedGeneration = runtime.activeGeneration(60U);
    if (!reusedGeneration) {
        gated->release.store(true);
        FAIL() << "Reused source generation was not created";
        return;
    }
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

TEST(PerSourceSpeechRuntime, CapacityIsolatesActiveTrack) {
    ThresholdVad vad;
    PerSourceSpeechOptions options{};
    options.max_sources = 1U;
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), options};
    const std::vector<float> voice(4U, 0.5F), quiet(4U, 0.0F);
    runtime.push(11U, chunk(voice));
    const auto first_generation = runtime.activeGeneration(11U);
    ASSERT_TRUE(first_generation.has_value());
    runtime.push(12U, chunk(voice));
    EXPECT_FALSE(runtime.trackStatus(12U).exists);
    EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
    EXPECT_EQ(runtime.activeGeneration(11U), first_generation);
    runtime.push(11U, chunk(quiet));
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].track_id, 11U);
    runtime.forget(11U);
    runtime.push(12U, chunk(voice));
    EXPECT_TRUE(runtime.activeGeneration(12U).has_value());
}

TEST(PerSourceSpeechRuntime, RejectsInvalidConstructorLimits) {
    ThresholdVad vad;
    const auto asr = std::make_shared<MockAsr>();
    auto reject = [&](PerSourceSpeechOptions options) {
        EXPECT_THROW(PerSourceSpeechRuntime(vad, asr, options), std::invalid_argument);
    };
    PerSourceSpeechOptions options{};
    options.sample_rate_hz = 0U;
    reject(options);
    options = {};
    options.max_sources = 0U;
    reject(options);
    options = {};
    options.max_pending_segments = 0U;
    reject(options);
    options = {};
    options.max_input_chunk_samples = 16001U;
    reject(options);
    options = {};
    options.max_retired_generations = 0U;
    reject(options);
    options = {};
    options.min_speech_samples = 0U;
    reject(options);
    options = {};
    options.max_segment_samples = 2U;
    reject(options);
    options = {};
    options.pre_roll_samples = options.max_segment_samples + 1U;
    reject(options);
    options = {};
    options.post_roll_samples = options.max_segment_samples + 1U;
    reject(options);
    EXPECT_THROW(PerSourceSpeechRuntime(vad, nullptr), std::invalid_argument);
}


namespace {
class FailingSessionVad final : public IVoiceActivityDetector {
public:
    enum class Failure { NullSession, StandardException, UnknownException };
    explicit FailingSessionVad(Failure failure) : failure_(failure) {}
    BackendInfo backendInfo() const override { return {"test-session-failure", "1"}; }
    AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<IVadSession> createSession() const override {
        if (failure_ == Failure::NullSession) return nullptr;
        if (failure_ == Failure::StandardException)
            throw std::runtime_error{"cannot create VAD session"};
        throw 17;
    }
private:
    Failure failure_;
};
} // namespace

TEST(PerSourceSpeechRuntime, FailedVadSessionIsQuarantinedPerTrack) {
    for (const auto failure : {
             FailingSessionVad::Failure::NullSession,
             FailingSessionVad::Failure::StandardException,
             FailingSessionVad::Failure::UnknownException}) {
        FailingSessionVad vad{failure};
        PerSourceSpeechOptions options{};
        options.min_speech_samples = 4U;
        PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), options};
        const std::vector<float> input(4U, 0.5F);
        EXPECT_NO_THROW(runtime.push(1U, chunk(input)));
        const auto first = runtime.trackStatus(1U);
        ASSERT_TRUE(first.exists);
        ASSERT_TRUE(first.error.has_value());
        EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
        EXPECT_NO_THROW(runtime.push(1U, chunk(input)));
        EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
        EXPECT_NO_THROW(runtime.push(2U, chunk(input)));
        EXPECT_TRUE(runtime.trackStatus(2U).error.has_value());
        EXPECT_EQ(runtime.statistics().dropped_segments, 2U);
        runtime.forget(1U);
        EXPECT_FALSE(runtime.trackStatus(1U).exists);
        EXPECT_TRUE(runtime.takeResults().empty());
    }
}


namespace {
class FirstTrackSessionFailureVad final : public IVoiceActivityDetector {
public:
    BackendInfo backendInfo() const override { return {"selective-vad-failure", "1"}; }
    AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<IVadSession> createSession() const override {
        if (calls_++ == 0U) throw std::runtime_error{"first VAD session failed"};
        return std::make_unique<ThresholdSession>();
    }
private:
    mutable std::size_t calls_{0U};
};
} // namespace

TEST(PerSourceSpeechRuntime, VadInitializationFailureDoesNotBlockHealthyTrack) {
    FirstTrackSessionFailureVad vad;
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), options};
    const std::vector<float> voice(4U, 0.5F), quiet(4U, 0.0F);
    runtime.push(100U, chunk(voice));
    ASSERT_TRUE(runtime.trackStatus(100U).error.has_value());
    runtime.push(101U, chunk(voice));
    runtime.push(101U, chunk(quiet));
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().track_id, 101U);
    EXPECT_EQ(results.front().transcript.text, "101:4");
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
    EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
    EXPECT_TRUE(runtime.trackStatus(100U).error.has_value());
}


namespace {
class NonstandardFailureAsr final : public IAsrEngine {
public:
    BackendInfo backendInfo() const override { return {"nonstandard-failure-asr", "1"}; }
    AsrCapabilities capabilities() const override { return {}; }
    Transcript transcribe(const SpeechSegment& segment) const override {
        if (segment.track_id->value() == 201U) throw 42;
        Transcript out{};
        out.segment_id = segment.segment_id;
        out.text = "healthy";
        return out;
    }
};
} // namespace

TEST(PerSourceSpeechRuntime, UnknownAsrExceptionDoesNotStopWorker) {
    ThresholdVad vad;
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<NonstandardFailureAsr>(), options};
    const std::vector<float> voice(4U, 0.5F), silence(4U, 0.0F);
    runtime.push(201U, chunk(voice));
    runtime.push(201U, chunk(silence));
    runtime.push(202U, chunk(voice));
    runtime.push(202U, chunk(silence));
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().track_id, 202U);
    EXPECT_EQ(results.front().transcript.text, "healthy");
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
    EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
    ASSERT_TRUE(runtime.statistics().last_error.has_value());
    EXPECT_EQ(*runtime.statistics().last_error, "Unknown ASR exception");
}


TEST(PerSourceSpeechRuntime, ForgetCancelsQueuedOldGenerationBeforeTrackIdReuse) {
    ThresholdVad vad;
    auto gated = std::make_shared<SlowAsr>();
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, gated, options};
    const std::vector<float> voice(4U, 0.5F), silence(4U, 0.0F);
    runtime.push(70U, chunk(voice));
    runtime.push(70U, chunk(silence));
    for (int i = 0; i < 1000 && !gated->entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gated->entered.load()) {
        gated->release.store(true);
        FAIL() << "First ASR job did not enter worker";
        return;
    }
    const auto old_generation = runtime.activeGeneration(70U);
    runtime.push(70U, chunk(voice));
    runtime.push(70U, chunk(silence));
    EXPECT_EQ(runtime.statistics().pending_segments, 1U);
    runtime.forget(70U);
    EXPECT_FALSE(runtime.activeGeneration(70U).has_value());
    EXPECT_EQ(runtime.statistics().pending_segments, 0U);
    runtime.push(70U, chunk(voice));
    runtime.push(70U, chunk(silence));
    const auto new_generation = runtime.activeGeneration(70U);
    EXPECT_NE(new_generation, old_generation);
    gated->release.store(true);
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].generation, *new_generation);
    EXPECT_EQ(results[0].transcript.text, "2");
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
    EXPECT_GE(runtime.statistics().dropped_segments, 1U);
}


TEST(PerSourceSpeechRuntime, CannotReleaseRetiredGenerationWithInflightAsr) {
    ThresholdVad vad;
    auto gated = std::make_shared<SlowAsr>();
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, gated, options};
    const std::vector<float> voice(4U, 0.5F), quiet(4U, 0.0F);
    runtime.push(91U, chunk(voice));
    runtime.push(91U, chunk(quiet));
    for (int i = 0; i < 1000 && !gated->entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gated->entered.load()) {
        gated->release.store(true);
        FAIL() << "ASR worker did not start";
        return;
    }
    const auto generation = runtime.retire(91U);
    if (!generation.has_value()) {
        gated->release.store(true);
        FAIL() << "Retirement did not produce a generation";
        return;
    }
    EXPECT_EQ(runtime.pendingResults(*generation), 1U);
    EXPECT_THROW(runtime.releaseRetired(*generation), std::logic_error);
    EXPECT_TRUE(runtime.drainReady().ready_retired_generations.empty());
    gated->release.store(true);
    bool ready = false;
    for (int i = 0; i < 1000; ++i) {
        const auto drained = runtime.drainReady();
        if (!drained.ready_retired_generations.empty()) {
            EXPECT_EQ(drained.ready_retired_generations.front(), *generation);
            ASSERT_EQ(drained.results.size(), 1U);
            EXPECT_EQ(drained.results.front().generation, *generation);
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(ready);
    EXPECT_NO_THROW(runtime.releaseRetired(*generation));
    EXPECT_TRUE(runtime.drainReady().ready_retired_generations.empty());
}


TEST(PerSourceSpeechRuntime, EmptyInputAndUnknownTrackOperationsAreNoOps) {
    ThresholdVad vad;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>()};
    const std::vector<float> empty;
    runtime.push(404U, chunk(empty));
    EXPECT_FALSE(runtime.activeGeneration(404U).has_value());
    EXPECT_FALSE(runtime.trackStatus(404U).exists);
    EXPECT_FALSE(runtime.retire(404U).has_value());
    runtime.forget(404U);
    EXPECT_EQ(runtime.pendingResults(999U), 0U);
    EXPECT_NO_THROW(runtime.releaseRetired(999U));
    const auto drained = runtime.drainReady();
    EXPECT_TRUE(drained.results.empty());
    EXPECT_TRUE(drained.ready_retired_generations.empty());
    EXPECT_EQ(runtime.statistics().completed_segments, 0U);
    EXPECT_EQ(runtime.statistics().dropped_segments, 0U);
}


TEST(PerSourceSpeechRuntime, ResetInvalidatesGenerationWithoutReusingIt) {
    ThresholdVad vad;
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), options};
    const std::vector<float> voiced(4U, 0.5F), quiet(4U, 0.0F);
    runtime.push(23U, chunk(voiced));
    const auto before_reset = runtime.activeGeneration(23U);
    ASSERT_TRUE(before_reset.has_value());
    runtime.reset();
    EXPECT_FALSE(runtime.activeGeneration(23U).has_value());
    EXPECT_FALSE(runtime.trackStatus(23U).exists);
    EXPECT_TRUE(runtime.drainReady().results.empty());
    runtime.push(23U, chunk(voiced));
    const auto after_reset = runtime.activeGeneration(23U);
    ASSERT_TRUE(after_reset.has_value());
    EXPECT_NE(*after_reset, *before_reset);
    runtime.push(23U, chunk(quiet));
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().generation, *after_reset);
    EXPECT_EQ(results.front().track_id, 23U);
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
}


TEST(PerSourceSpeechRuntime, ResetDuringInflightAsrDoesNotPublishStaleResult) {
    ThresholdVad vad;
    auto gated = std::make_shared<SlowAsr>();
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, gated, options};
    const std::vector<float> voice(4U, 0.5F), silence(4U, 0.0F);
    runtime.push(81U, chunk(voice));
    runtime.push(81U, chunk(silence));
    for (int i = 0; i < 1000 && !gated->entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gated->entered.load()) {
        gated->release.store(true);
        FAIL() << "Initial ASR did not start";
        return;
    }
    const auto old_generation = runtime.activeGeneration(81U);
    ASSERT_TRUE(old_generation.has_value());
    runtime.reset();
    runtime.push(81U, chunk(voice));
    const auto new_generation = runtime.activeGeneration(81U);
    ASSERT_TRUE(new_generation.has_value());
    EXPECT_NE(*old_generation, *new_generation);
    runtime.push(81U, chunk(silence));
    gated->release.store(true);
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().track_id, 81U);
    EXPECT_EQ(results.front().generation, *new_generation);
    EXPECT_EQ(results.front().transcript.text, "2");
    EXPECT_TRUE(runtime.takeResults().empty());
}


TEST(PerSourceSpeechRuntime, RejectsMultichannelAndPlanarCaptureWithoutTrackMutation) {
    ThresholdVad vad;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>()};
    const std::vector<float> samples(8U, 0.5F);
    const Timestamp timestamp{};
    const AudioView stereo{samples.data(), samples.size(),
        {16000U, 2U, AudioLayout::Interleaved}, timestamp};
    const AudioView planar{samples.data(), samples.size(),
        {16000U, 1U, AudioLayout::Planar}, timestamp};
    EXPECT_THROW(runtime.push(75U, stereo), std::invalid_argument);
    EXPECT_THROW(runtime.push(75U, planar), std::invalid_argument);
    EXPECT_FALSE(runtime.trackStatus(75U).exists);
    EXPECT_FALSE(runtime.activeGeneration(75U).has_value());
    EXPECT_EQ(runtime.statistics().dropped_segments, 0U);
}


TEST(PerSourceSpeechRuntime, ResetClearsQueuedJobsWithoutLeakingOldGeneration) {
    ThresholdVad vad;
    auto gated = std::make_shared<SlowAsr>();
    PerSourceSpeechOptions options{};
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, gated, options};
    const std::vector<float> voice(4U, 0.5F), silence(4U, 0.0F);
    runtime.push(85U, chunk(voice));
    runtime.push(85U, chunk(silence));
    for (int i = 0; i < 1000 && !gated->entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gated->entered.load()) {
        gated->release.store(true);
        FAIL() << "Initial ASR job did not start";
        return;
    }
    runtime.push(85U, chunk(voice));
    runtime.push(85U, chunk(silence));
    EXPECT_EQ(runtime.statistics().pending_segments, 1U);
    const auto previous_generation = runtime.activeGeneration(85U);
    runtime.reset();
    EXPECT_EQ(runtime.statistics().pending_segments, 0U);
    EXPECT_FALSE(runtime.activeGeneration(85U).has_value());
    EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
    runtime.push(85U, chunk(voice));
    runtime.push(85U, chunk(silence));
    const auto current_generation = runtime.activeGeneration(85U);
    EXPECT_NE(previous_generation, current_generation);
    gated->release.store(true);
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().generation, *current_generation);
    EXPECT_EQ(results.front().transcript.text, "2");
    EXPECT_TRUE(runtime.takeResults().empty());
}


TEST(PerSourceSpeechRuntime, BoundedAsrQueueDropsOverflowAndRecovers) {
    ThresholdVad vad;
    auto gated = std::make_shared<SlowAsr>();
    PerSourceSpeechOptions options{};
    options.max_pending_segments = 1U;
    options.min_speech_samples = 4U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    PerSourceSpeechRuntime runtime{vad, gated, options};
    const std::vector<float> voice(4U, 0.5F), quiet(4U, 0.0F);
    runtime.push(301U, chunk(voice));
    runtime.push(301U, chunk(quiet));
    for (int i = 0; i < 1000 && !gated->entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gated->entered.load()) {
        gated->release.store(true);
        FAIL() << "ASR worker did not start";
        return;
    }
    runtime.push(302U, chunk(voice));
    runtime.push(302U, chunk(quiet));
    EXPECT_EQ(runtime.statistics().pending_segments, 1U);
    runtime.push(303U, chunk(voice));
    runtime.push(303U, chunk(quiet));
    EXPECT_EQ(runtime.statistics().pending_segments, 1U);
    EXPECT_EQ(runtime.statistics().dropped_segments, 1U);
    gated->release.store(true);
    bool completed = false;
    for (int i = 0; i < 1000; ++i) {
        if (runtime.statistics().completed_segments == 2U) {
            completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(completed);
    const auto results = runtime.takeResults();
    // The result mailbox shares the same bound as the pending ASR queue.
    // The second completion evicts the oldest unread result.
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].track_id, 302U);
    EXPECT_EQ(runtime.statistics().dropped_segments, 2U);
    EXPECT_EQ(runtime.statistics().evicted_results, 1U);
    EXPECT_EQ(runtime.statistics().buffered_results, 0U);
    EXPECT_EQ(runtime.statistics().pending_segments, 0U);
    runtime.push(303U, chunk(voice));
    runtime.push(303U, chunk(quiet));
    const auto recovered = awaitSpeech(runtime, 1U);
    ASSERT_EQ(recovered.size(), 1U);
    EXPECT_EQ(recovered.front().track_id, 303U);
}


TEST(PerSourceSpeechRuntime, HomogeneousVadChunksPreservePreAndPostRollSegmentation) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 6U;
    opts.pre_roll_samples = 3U;
    opts.post_roll_samples = 5U;
    opts.max_segment_samples = 64U;
    const auto run = [&](const std::vector<std::size_t>& pre,
                         const std::vector<std::size_t>& voice,
                         const std::vector<std::size_t>& trailing) {
        PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), opts};
        const auto send = [&](float sample, const std::vector<std::size_t>& sizes) {
            for (const auto length : sizes) {
                const std::vector<float> audio(length, sample);
                runtime.push(415U, chunk(audio));
            }
        };
        send(0.F, pre);
        send(0.5F, voice);
        send(0.F, trailing);
        auto results = awaitSpeech(runtime, 1U);
        EXPECT_EQ(runtime.statistics().completed_segments, 1U);
        EXPECT_EQ(runtime.statistics().dropped_segments, 0U);
        EXPECT_TRUE(runtime.takeResults().empty());
        return results;
    };
    const auto monolithic = run({3U}, {6U}, {8U});
    const auto fragmented = run({1U, 2U}, {1U, 2U, 3U}, {2U, 1U, 2U, 3U});
    ASSERT_EQ(monolithic.size(), 1U);
    ASSERT_EQ(fragmented.size(), 1U);
    EXPECT_EQ(monolithic[0].transcript.text, "415:14");
    EXPECT_EQ(fragmented[0].transcript.text, monolithic[0].transcript.text);
    EXPECT_EQ(fragmented[0].start_time.nanoseconds(),
              monolithic[0].start_time.nanoseconds());
    EXPECT_EQ(fragmented[0].end_time.nanoseconds(),
              monolithic[0].end_time.nanoseconds());
}

TEST(PerSourceSpeechRuntime, PartialPostRollDiscardedOnResetWithoutGhostSegment) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 4U;
    opts.pre_roll_samples = 0U;
    opts.post_roll_samples = 5U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<MockAsr>(), opts};
    const std::vector<float> voice(4U, 0.5F), quiet2(2U, 0.F),
                             quiet3(3U, 0.F);
    runtime.push(71U, chunk(voice));
    runtime.push(71U, chunk(quiet2));
    const auto before = runtime.activeGeneration(71U);
    ASSERT_TRUE(before.has_value());
    EXPECT_TRUE(runtime.takeResults().empty());
    runtime.reset();
    EXPECT_FALSE(runtime.trackStatus(71U).exists);
    runtime.push(71U, chunk(voice));
    const auto after = runtime.activeGeneration(71U);
    ASSERT_TRUE(after.has_value());
    EXPECT_NE(*before, *after);
    runtime.push(71U, chunk(quiet2));
    runtime.push(71U, chunk(quiet3));
    const auto results = awaitSpeech(runtime, 1U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].transcript.text, "71:9");
    EXPECT_EQ(results[0].generation, *after);
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
}

TEST(PerSourceSpeechRuntime, UnpolledTranscriptsStayBoundedAndExposeEvictions) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.max_pending_segments=2U;
    opts.min_speech_samples=1U;
    opts.max_segment_samples=4U;
    opts.pre_roll_samples=0U;
    opts.post_roll_samples=0U;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>(),opts};
    const std::vector<float> speech(4U,0.5F);
    constexpr std::size_t completed_count=8U;
    for (std::size_t i=0;i<completed_count;++i) {
        runtime.push(409U,chunk(speech)); // segment cap completes the utterance
        bool completed=false;
        for (int spin=0;spin<1500;++spin) {
            if (runtime.statistics().completed_segments==i+1U) {
                completed=true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        ASSERT_TRUE(completed) << "ASR completion "<<i;
        const auto stats=runtime.statistics();
        EXPECT_EQ(stats.pending_segments,0U);
        EXPECT_EQ(stats.buffered_results,std::min(i+1U,opts.max_pending_segments));
        EXPECT_EQ(stats.evicted_results,
                  (i+1U>opts.max_pending_segments)
                      ? i+1U-opts.max_pending_segments : 0U);
    }
    const auto held=runtime.takeResults();
    ASSERT_EQ(held.size(),opts.max_pending_segments);
    EXPECT_EQ(held[0].track_id,409U);
    EXPECT_EQ(held[1].track_id,409U);
    EXPECT_NE(held[0].transcript.segment_id,held[1].transcript.segment_id);
    EXPECT_EQ(runtime.statistics().buffered_results,0U);
    EXPECT_EQ(runtime.statistics().evicted_results,completed_count-2U);

    runtime.push(409U,chunk(speech));
    bool recovered=false;
    for (int spin=0;spin<1500;++spin) {
        if (runtime.statistics().completed_segments==completed_count+1U) {
            recovered=true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(recovered);
    const auto latest=runtime.takeResults();
    ASSERT_EQ(latest.size(),1U);
    EXPECT_EQ(latest.front().transcript.text,"409:4");
    EXPECT_EQ(runtime.statistics().evicted_results,completed_count-2U);
}

namespace {
class BudgetAsr final : public IAsrEngine {
public:
    BackendInfo backendInfo() const override { return {"budget-asr", "1"}; }
    AsrCapabilities capabilities() const override { return {}; }
    Transcript transcribe(const SpeechSegment& segment) const override {
        Transcript result{};
        result.segment_id = segment.segment_id;
        const auto id = segment.track_id->value();
        if (id == 700U) result.text = std::string(9U, 'x');
        else if (id == 701U) result.tokens.push_back({std::string(9U, 't'), {}, {}, {}});
        else if (id == 702U) {
            result.words.push_back({"a", {}, {}, {}});
            result.words.push_back({"b", {}, {}, {}});
        } else {
            result.text = "ok";
            result.language = "en";
        }
        return result;
    }
};
}

TEST(PerSourceSpeechRuntime, BoundedTranscriptBytesAndItemsPreserveOtherSources) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.min_speech_samples = 4U;
    opts.pre_roll_samples = 0U;
    opts.post_roll_samples = 0U;
    opts.max_result_text_bytes = 8U;
    opts.max_result_items = 1U;
    PerSourceSpeechRuntime runtime{vad, std::make_shared<BudgetAsr>(), opts};
    const std::vector<float> speech(4U, 0.5F), silence(4U, 0.F);
    for (const auto id : {700U, 701U, 702U, 703U}) {
        runtime.push(id, chunk(speech));
        runtime.push(id, chunk(silence));
    }
    bool settled = false;
    for (int spin = 0; spin < 1500; ++spin) {
        const auto stats = runtime.statistics();
        if (stats.completed_segments + stats.dropped_segments == 4U) {
            settled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(settled);
    const auto results = runtime.takeResults();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().track_id, 703U);
    EXPECT_EQ(results.front().transcript.text, "ok");
    EXPECT_EQ(runtime.statistics().completed_segments, 1U);
    EXPECT_EQ(runtime.statistics().dropped_segments, 3U);
    EXPECT_EQ(runtime.statistics().evicted_results, 0U);
    ASSERT_TRUE(runtime.statistics().last_error.has_value());
    EXPECT_EQ(*runtime.statistics().last_error,
              "ASR transcript exceeds configured result budget");
    runtime.push(703U, chunk(speech));
    runtime.push(703U, chunk(silence));
    const auto recovered = awaitSpeech(runtime, 1U);
    ASSERT_EQ(recovered.size(), 1U);
    EXPECT_EQ(recovered.front().track_id, 703U);
}

TEST(PerSourceSpeechRuntime, RejectsZeroTranscriptBudgets) {
    ThresholdVad vad;
    const auto asr = std::make_shared<MockAsr>();
    PerSourceSpeechOptions opts{};
    opts.max_result_text_bytes = 0U;
    EXPECT_THROW((PerSourceSpeechRuntime{vad, asr, opts}), std::invalid_argument);
    opts = {};
    opts.max_result_items = 0U;
    EXPECT_THROW((PerSourceSpeechRuntime{vad, asr, opts}), std::invalid_argument);
}

TEST(PerSourceSpeechRuntime, RetiredGenerationCapacityIsBoundedAndRetryIsTransactional) {
    ThresholdVad vad;
    PerSourceSpeechOptions opts{};
    opts.max_retired_generations=1U;
    opts.min_speech_samples=4U;
    opts.pre_roll_samples=0U;
    opts.post_roll_samples=0U;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>(),opts};
    const std::vector<float> quiet(4U,0.0F), voice(4U,0.5F);

    runtime.push(501U,chunk(quiet));
    const auto first=runtime.retire(501U);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(runtime.statistics().retired_generations,1U);

    runtime.push(502U,chunk(voice));
    const auto second_generation=runtime.activeGeneration(502U);
    ASSERT_TRUE(second_generation.has_value());
    EXPECT_FALSE(runtime.retire(502U).has_value());
    EXPECT_EQ(runtime.activeGeneration(502U),second_generation);
    EXPECT_EQ(runtime.trackStatus(502U).buffered_samples,4U);
    EXPECT_EQ(runtime.statistics().pending_segments,0U);
    EXPECT_EQ(runtime.statistics().retired_generations,1U);

    const auto ready=runtime.drainReady();
    EXPECT_TRUE(ready.results.empty());
    ASSERT_EQ(ready.ready_retired_generations.size(),1U);
    EXPECT_EQ(ready.ready_retired_generations.front(),*first);
    runtime.releaseRetired(*first);
    EXPECT_EQ(runtime.statistics().retired_generations,0U);

    const auto second=runtime.retire(502U);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second,*second_generation);
    const auto result=awaitSpeech(runtime,1U);
    ASSERT_EQ(result.size(),1U);
    EXPECT_EQ(result.front().generation,*second);
    EXPECT_EQ(result.front().transcript.text,"502:4");
    const auto settled=runtime.drainReady();
    ASSERT_EQ(settled.ready_retired_generations.size(),1U);
    EXPECT_EQ(settled.ready_retired_generations.front(),*second);
    runtime.releaseRetired(*second);
    EXPECT_EQ(runtime.statistics().retired_generations,0U);

    // Repeated retirement/release must not consume capacity permanently.
    for (std::uint64_t id=503U;id<535U;++id) {
        runtime.push(id,chunk(quiet));
        const auto gen=runtime.retire(id);
        ASSERT_TRUE(gen.has_value()) << "id="<<id;
        EXPECT_EQ(runtime.statistics().retired_generations,1U);
        runtime.releaseRetired(*gen);
        EXPECT_EQ(runtime.statistics().retired_generations,0U);
    }
}

TEST(PerSourceSpeechRuntime, FullRetirementTableCanDiscardVanishedSourceSafely) {
    ThresholdVad vad;
    auto slow=std::make_shared<SlowAsr>();
    PerSourceSpeechOptions cfg{};
    cfg.max_retired_generations=1U;
    cfg.min_speech_samples=4U;
    cfg.pre_roll_samples=0U;
    cfg.post_roll_samples=0U;
    PerSourceSpeechRuntime runtime{vad,slow,cfg};
    const std::vector<float> voice(4U,0.5F),silence(4U,0.0F);
    runtime.push(610U,chunk(voice));
    runtime.push(610U,chunk(silence));
    for (int spin=0;spin<1000 && !slow->entered.load();++spin)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!slow->entered.load()) {
        slow->release.store(true);
        FAIL() << "ASR worker did not begin";
        return;
    }
    const auto first=runtime.retire(610U);
    ASSERT_TRUE(first.has_value());
    runtime.push(611U,chunk(voice));
    const auto second=runtime.activeGeneration(611U);
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(runtime.retire(611U).has_value());
    const auto attempted_drain=runtime.drainReady();
    EXPECT_TRUE(attempted_drain.ready_retired_generations.empty());
    EXPECT_EQ(runtime.activeGeneration(611U),second);
    // Consumer's explicit loss fallback must not cancel earlier ASR.
    runtime.forget(611U);
    EXPECT_FALSE(runtime.activeGeneration(611U).has_value());
    EXPECT_EQ(runtime.statistics().retired_generations,1U);
    slow->release.store(true);
    bool ready=false;
    for (int spin=0;spin<1000;++spin) {
        auto drained=runtime.drainReady();
        if (!drained.ready_retired_generations.empty()) {
            ASSERT_EQ(drained.ready_retired_generations.size(),1U);
            EXPECT_EQ(drained.ready_retired_generations.front(),*first);
            ASSERT_EQ(drained.results.size(),1U);
            EXPECT_EQ(drained.results.front().generation,*first);
            ready=true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(ready);
    runtime.releaseRetired(*first);
    EXPECT_EQ(runtime.statistics().retired_generations,0U);
    runtime.push(611U,chunk(voice));
    const auto fresh=runtime.activeGeneration(611U);
    ASSERT_TRUE(fresh.has_value());
    EXPECT_NE(*fresh,*second);
    runtime.push(611U,chunk(silence));
    const auto recovered=awaitSpeech(runtime,1U);
    ASSERT_EQ(recovered.size(),1U);
    EXPECT_EQ(recovered.front().generation,*fresh);
    EXPECT_EQ(recovered.front().transcript.text,"2");
}

TEST(PerSourceSpeechRuntime, OversizedInputChunkPreservesExistingAndUnseenSources) {
    ThresholdVad vad;
    PerSourceSpeechOptions cfg{};
    cfg.max_input_chunk_samples=8U;
    cfg.min_speech_samples=4U;
    cfg.max_segment_samples=64U;
    cfg.pre_roll_samples=0U;
    cfg.post_roll_samples=0U;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<MockAsr>(),cfg};
    const std::vector<float> voice(4U,0.5F),quiet(4U,0.0F);
    const std::vector<float> too_long(9U,0.5F);
    runtime.push(510U,chunk(voice));
    const auto generation=runtime.activeGeneration(510U);
    ASSERT_TRUE(generation.has_value());
    EXPECT_EQ(runtime.trackStatus(510U).buffered_samples,4U);
    EXPECT_THROW(runtime.push(510U,chunk(too_long)),std::length_error);
    EXPECT_THROW(runtime.push(511U,chunk(too_long)),std::length_error);
    EXPECT_EQ(runtime.activeGeneration(510U),generation);
    EXPECT_EQ(runtime.trackStatus(510U).buffered_samples,4U);
    EXPECT_FALSE(runtime.activeGeneration(511U).has_value());
    EXPECT_FALSE(runtime.trackStatus(511U).exists);
    EXPECT_EQ(runtime.statistics().completed_segments,0U);
    EXPECT_EQ(runtime.statistics().dropped_segments,0U);
    runtime.push(510U,chunk(quiet));
    const auto done=awaitSpeech(runtime,1U);
    ASSERT_EQ(done.size(),1U);
    EXPECT_EQ(done.front().generation,*generation);
    EXPECT_EQ(done.front().transcript.text,"510:4");
    EXPECT_NO_THROW(runtime.push(512U,chunk(std::vector<float>(8U,0.0F))));
    EXPECT_TRUE(runtime.trackStatus(512U).exists);

    // Unspecified limit equals one second at the configured sample rate.
    PerSourceSpeechRuntime standard{vad,std::make_shared<MockAsr>()};
    const std::vector<float> default_too_long(16001U,0.5F);
    EXPECT_THROW(standard.push(513U,chunk(default_too_long)),std::length_error);
    EXPECT_FALSE(standard.activeGeneration(513U).has_value());
}

namespace {
class ConstrainedVad final : public IVoiceActivityDetector {
public:
    explicit ConstrainedVad(AudioRequirements requirements)
        : requirements_(std::move(requirements)) {}
    BackendInfo backendInfo() const override { return {"constrained-vad","1"}; }
    AudioRequirements audioRequirements() const override { return requirements_; }
    std::unique_ptr<IVadSession> createSession() const override {
        return std::make_unique<ThresholdSession>();
    }
private:
    AudioRequirements requirements_{};
};
class ConstrainedAsr final : public IAsrEngine {
public:
    explicit ConstrainedAsr(AudioRequirements requirements)
        : requirements_(std::move(requirements)) {}
    BackendInfo backendInfo() const override { return {"constrained-asr","1"}; }
    AsrCapabilities capabilities() const override {
        AsrCapabilities capabilities{};
        capabilities.audio=requirements_;
        return capabilities;
    }
    Transcript transcribe(const SpeechSegment& segment) const override {
        Transcript result{};
        result.segment_id=segment.segment_id;
        result.text="compatible";
        return result;
    }
private:
    AudioRequirements requirements_{};
};
} // namespace

TEST(PerSourceSpeechRuntime, RejectsIncompatibleVadAndAsrAudioCapabilities) {
    PerSourceSpeechOptions options{};
    options.min_speech_samples=4U;
    options.pre_roll_samples=0U;
    options.post_roll_samples=0U;
    const auto good=AudioRequirements{};
    ConstrainedVad good_vad{good};
    const auto good_asr=std::make_shared<ConstrainedAsr>(good);
    const auto fails_vad=[&](AudioRequirements requirement) {
        ConstrainedVad vad{std::move(requirement)};
        EXPECT_THROW((PerSourceSpeechRuntime{vad,good_asr,options}),
                     std::invalid_argument);
    };
    const auto fails_asr=[&](AudioRequirements requirement) {
        auto asr=std::make_shared<ConstrainedAsr>(std::move(requirement));
        EXPECT_THROW((PerSourceSpeechRuntime{good_vad,asr,options}),
                     std::invalid_argument);
    };
    AudioRequirements wrong_rate{};
    wrong_rate.supported_sample_rates_hz={8000U,48000U};
    fails_vad(wrong_rate);
    fails_asr(wrong_rate);

    AudioRequirements stereo_only{};
    stereo_only.min_channels=2U;
    stereo_only.max_channels=2U;
    fails_vad(stereo_only);
    fails_asr(stereo_only);

    AudioRequirements planar_only{};
    planar_only.supported_layouts={AudioLayout::Planar};
    fails_vad(planar_only);
    fails_asr(planar_only);

    AudioRequirements invalid_range{};
    invalid_range.max_channels=0U;
    fails_vad(invalid_range);
    fails_asr(invalid_range);

    // Explicit matching capabilities must still construct successfully.
    AudioRequirements matching{};
    matching.supported_sample_rates_hz={16000U};
    matching.supported_layouts={AudioLayout::Interleaved};
    ConstrainedVad matching_vad{matching};
    auto matching_asr=std::make_shared<ConstrainedAsr>(matching);
    PerSourceSpeechRuntime runtime{matching_vad,matching_asr,options};
    const std::vector<float> voice(4U,0.5F),silence(4U,0.0F);
    runtime.push(772U,chunk(voice));
    runtime.push(772U,chunk(silence));
    const auto results=awaitSpeech(runtime,1U);
    ASSERT_EQ(results.size(),1U);
    EXPECT_EQ(results.front().transcript.text,"compatible");
}
