#include <audition/speech/async_speech_ingress_worker.hpp>
#include <audition/recording/source_recording_session.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace audition;

namespace {
class WorkerVadSession final : public IVadSession {
public:
    void reset() override {}
    VadResult process(AudioView audio) override {
        bool active=false;
        for(std::size_t i=0;i<audio.sampleCount();++i)
            active=active||audio.data()[i]>0.2F;
        return {std::nullopt,active};
    }
};
class WorkerVad final : public IVoiceActivityDetector {
public:
    BackendInfo backendInfo() const override { return {"test-vad","1"}; }
    AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<IVadSession> createSession() const override {
        return std::make_unique<WorkerVadSession>();
    }
};
class WorkerAsr final : public IAsrEngine {
public:
    BackendInfo backendInfo() const override { return {"test-asr","1"}; }
    AsrCapabilities capabilities() const override { return {}; }
    Transcript transcribe(const SpeechSegment& segment) const override {
        Transcript value{};
        value.segment_id=segment.segment_id;
        value.text=std::to_string(segment.audio.samples().size());
        return value;
    }
};
AudioView audioBlock(const std::vector<float>& pcm,Timestamp timestamp={}) {
    return AudioView{pcm.data(),pcm.size(),
        {16000U,1U,AudioLayout::Interleaved},timestamp};
}
PerSourceSpeechOptions speechOptions() {
    PerSourceSpeechOptions options{};
    options.min_speech_samples=4U;
    options.pre_roll_samples=0U;
    options.post_roll_samples=0U;
    return options;
}
} // namespace

TEST(AsyncSpeechIngressWorker, DeferredRecordingRetireSavesLastUtterance) {
    WorkerVad vad{};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    const Timestamp origin{1000000000LL,{ClockDomain::Monotonic,17U}};
    const auto root=std::filesystem::temp_directory_path() /
        ("audition-worker-retire-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto wav=root/"first.wav";
    recording::SourceRecordingSession recordings{};
    recordings.begin(71U,1001U,wav,16000U,origin);
    std::optional<std::uint64_t> source_epoch{};
    {
        AsyncSpeechIngressWorker worker{speech};
        const std::vector<float> voiced(4U,0.5F);
        ASSERT_EQ(worker.push(71U,audioBlock(voiced,origin)),
                  SpeechPcmAdmission::Accepted);
        source_epoch=worker.sourceEpoch(71U);
        ASSERT_TRUE(source_epoch.has_value());
        const auto retired_epoch=worker.retire(71U);
        ASSERT_EQ(retired_epoch,source_epoch);
        const auto deferred=recordings.retireDeferred(71U);
        ASSERT_EQ(deferred,1001U);
        worker.closeInput();

        bool saw_bound=false,saw_retired=false;
        for(int attempt=0;attempt<1000 && !worker.finished();++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        ASSERT_TRUE(worker.finished());
        for (const auto& event:worker.drainAcknowledgements()) {
            EXPECT_EQ(event.source_id,71U);
            EXPECT_EQ(event.epoch,*source_epoch);
            if (event.kind==AsyncSpeechIngressWorker::Acknowledgement::Kind::Bound) {
                ASSERT_TRUE(event.generation.has_value());
                recordings.bindRecordingAsr(*deferred,*event.generation);
                saw_bound=true;
            } else if (event.kind==AsyncSpeechIngressWorker::Acknowledgement::Kind::Retired) {
                ASSERT_TRUE(event.generation.has_value());
                recordings.acknowledgeRetired(*deferred,event.generation);
                saw_retired=true;
            } else {
                FAIL() << "Unexpected worker error/invalidation";
            }
        }
        EXPECT_TRUE(saw_bound && saw_retired);
    }

    std::vector<SourceSpeechResult> received;
    for(int attempt=0;attempt<1000 && received.empty();++attempt) {
        auto batch=speech.drainReady();
        received=std::move(batch.results);
        if(received.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(received.size(),1U);
    EXPECT_EQ(received.front().track_id,71U);
    EXPECT_EQ(received.front().transcript.text,"4");
    recordings.addSpeech(received.front());
    speech.releaseRetired(received.front().generation);
    recordings.completeGeneration(received.front().generation);
    EXPECT_EQ(recordings.retainedRecordings(),0U);
    const auto saved=readRecordingTimelineJson(
        std::filesystem::path{wav.string()+".json"});
    ASSERT_EQ(saved.speech().size(),1U);
    EXPECT_EQ(saved.speech().front().text,"4");
    std::filesystem::remove_all(root);
}

TEST(AsyncSpeechIngressWorker, BoundedAcknowledgementMailboxResumesAfterDrain) {
    WorkerVad vad{};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    AsyncSpeechIngressWorker worker{speech,SpeechPcmIngressOptions{},1U};
    const std::vector<float> pcm(4U,0.5F);
    ASSERT_EQ(worker.push(99U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    ASSERT_TRUE(worker.retire(99U).has_value());
    worker.closeInput();
    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int attempt=0;attempt<1500 && (!worker.finished() || events.size()<2U);
        ++attempt) {
        auto batch=worker.drainAcknowledgements();
        for(auto& item:batch)events.push_back(std::move(item));
        if(!worker.finished() || events.size()<2U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(worker.finished());
    ASSERT_EQ(events.size(),2U);
    EXPECT_EQ(events[0].kind,AsyncSpeechIngressWorker::Acknowledgement::Kind::Bound);
    EXPECT_EQ(events[1].kind,AsyncSpeechIngressWorker::Acknowledgement::Kind::Retired);
    ASSERT_TRUE(events[0].generation.has_value());
    EXPECT_EQ(events[0].generation,events[1].generation);
}

TEST(AsyncSpeechIngressWorker, CloseWithoutInputReleasesWorker) {
    WorkerVad vad{};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    AsyncSpeechIngressWorker worker{speech};
    worker.closeInput();
    for(int attempt=0;attempt<1000 && !worker.finished();++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    EXPECT_TRUE(worker.finished());
    const std::vector<float> pcm(4U,0.2F);
    EXPECT_EQ(worker.push(1U,audioBlock(pcm)),SpeechPcmAdmission::Closed);
    EXPECT_TRUE(worker.drainAcknowledgements().empty());
}
