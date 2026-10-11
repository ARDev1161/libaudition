#include <audition/speech/per_source_speech_runtime.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
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
