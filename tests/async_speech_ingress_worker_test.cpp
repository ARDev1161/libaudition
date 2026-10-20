#include <audition/speech/async_speech_ingress_worker.hpp>
#include <audition/recording/source_recording_session.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
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

namespace {
struct PausedVadGate {
    std::atomic<bool> entered{false};
    std::atomic<bool> resume{false};
};
class PausedVadSession final : public IVadSession {
public:
    explicit PausedVadSession(std::shared_ptr<PausedVadGate> gate)
        : gate_(std::move(gate)) {}
    void reset() override {}
    VadResult process(AudioView) override {
        gate_->entered.store(true);
        while (!gate_->resume.load())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        return {std::nullopt,true};
    }
private:
    std::shared_ptr<PausedVadGate> gate_;
};
class PausedVad final : public IVoiceActivityDetector {
public:
    explicit PausedVad(std::shared_ptr<PausedVadGate> gate)
        : gate_(std::move(gate)) {}
    BackendInfo backendInfo() const override { return {"paused-vad","1"}; }
    AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<IVadSession> createSession() const override {
        return std::make_unique<PausedVadSession>(gate_);
    }
private:
    std::shared_ptr<PausedVadGate> gate_;
};
}

TEST(AsyncSpeechIngressWorker, OverflowInvalidatesGenerationBeforeReusedRetirement) {
    auto gate=std::make_shared<PausedVadGate>();
    PausedVad vad{gate};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    SpeechPcmIngressOptions cfg{};
    cfg.max_sources=1U;
    cfg.max_pending_chunks=1U;
    cfg.max_pending_samples=4U;
    cfg.max_chunk_samples=4U;
    AsyncSpeechIngressWorker worker{speech,cfg};
    const std::vector<float> pcm(4U,0.5F);
    EXPECT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto old_epoch=worker.sourceEpoch(71U);
    for (int i=0;i<2000 && !gate->entered.load();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if (!gate->entered.load()) {
        gate->resume.store(true);
        FAIL() << "VAD did not enter the test gate";
        return;
    }
    EXPECT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    EXPECT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::RejectedCapacity);
    EXPECT_EQ(worker.inputStatistics().invalidated_chunks,1U);
    EXPECT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=worker.sourceEpoch(71U);
    EXPECT_NE(old_epoch,new_epoch);
    EXPECT_EQ(worker.retire(71U),new_epoch);
    worker.closeInput();
    gate->resume.store(true);

    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for (int i=0;i<2000 && (!worker.finished() || events.size()<4U);++i) {
        auto batch=worker.drainAcknowledgements();
        for (auto& event:batch) events.push_back(std::move(event));
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(worker.finished());
    ASSERT_EQ(events.size(),4U);
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    EXPECT_EQ(events[0].kind,Kind::Bound);
    EXPECT_EQ(events[1].kind,Kind::Forgot);
    EXPECT_EQ(events[2].kind,Kind::Bound);
    EXPECT_EQ(events[3].kind,Kind::Retired);
    EXPECT_EQ(events[0].epoch,old_epoch.value_or(0U));
    EXPECT_EQ(events[1].epoch,new_epoch.value_or(0U));
    EXPECT_EQ(events[2].epoch,new_epoch.value_or(0U));
    EXPECT_EQ(events[3].epoch,new_epoch.value_or(0U));
    EXPECT_EQ(events[0].generation,events[1].generation);
    EXPECT_NE(events[0].generation,events[2].generation);
    EXPECT_EQ(events[2].generation,events[3].generation);
    EXPECT_EQ(worker.inputStatistics().outstanding_source_slots,0U);
}


namespace {
class FailingSecondVadSession final : public IVadSession {
public:
    void reset() override { calls_=0U; }
    VadResult process(AudioView) override {
        if (++calls_==2U) throw std::runtime_error{"synthetic VAD failure"};
        return {std::nullopt,true};
    }
private:
    std::size_t calls_{0U};
};
class FailingSecondVad final : public IVoiceActivityDetector {
public:
    BackendInfo backendInfo() const override { return {"failing-vad","1"}; }
    AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<IVadSession> createSession() const override {
        return std::make_unique<FailingSecondVadSession>();
    }
};
} // namespace

TEST(AsyncSpeechIngressWorker, VadFailureCancelsGenerationAndAllowsReusedSource) {
    FailingSecondVad vad{};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    AsyncSpeechIngressWorker worker{speech};
    const std::vector<float> pcm(4U,0.5F);
    ASSERT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto first_epoch=worker.sourceEpoch(71U);
    ASSERT_TRUE(first_epoch.has_value());
    ASSERT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);

    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int i=0;i<2000 && events.size()<3U;++i) {
        auto batch=worker.drainAcknowledgements();
        for(auto& event:batch) events.push_back(std::move(event));
        if(events.size()<3U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(events.size(),3U);
    EXPECT_EQ(events[0].kind,Kind::Bound);
    EXPECT_EQ(events[1].kind,Kind::Error);
    EXPECT_EQ(events[2].kind,Kind::Forgot);
    ASSERT_TRUE(events[0].generation.has_value());
    EXPECT_EQ(events[1].generation,events[0].generation);
    EXPECT_EQ(events[1].error,"synthetic VAD failure");
    EXPECT_EQ(events[0].epoch,*first_epoch);
    EXPECT_EQ(events[1].epoch,*first_epoch);
    EXPECT_NE(events[2].epoch,*first_epoch);
    EXPECT_FALSE(speech.activeGeneration(71U).has_value());
    EXPECT_EQ(worker.inputStatistics().outstanding_source_slots,0U);

    ASSERT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto reused_epoch=worker.sourceEpoch(71U);
    ASSERT_TRUE(reused_epoch.has_value());
    EXPECT_NE(reused_epoch,first_epoch);
    worker.closeInput();
    for(int i=0;i<2000 && !worker.finished();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    ASSERT_TRUE(worker.finished());
    const auto resumed=worker.drainAcknowledgements();
    ASSERT_EQ(resumed.size(),1U);
    EXPECT_EQ(resumed[0].kind,Kind::Bound);
    EXPECT_EQ(resumed[0].epoch,*reused_epoch);
    ASSERT_TRUE(resumed[0].generation.has_value());
    EXPECT_NE(resumed[0].generation,events[0].generation);
}
