#include <audition/speech/async_speech_ingress_worker.hpp>
#include <audition/recording/source_recording_session.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#include "../demo/qt/speech_epoch_recording_cleanup.hpp"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
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
    std::atomic<bool> fail_once{false};
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
        if (gate_->fail_once.exchange(false))
            throw std::runtime_error{"delayed stale-epoch VAD failure"};
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

TEST(AsyncSpeechIngressWorker, StaleVadFailurePreservesReusedPcmAndRetirement) {
    auto gate=std::make_shared<PausedVadGate>();
    PausedVad vad{gate};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    AsyncSpeechIngressWorker worker{speech};
    const std::vector<float> pcm(4U,0.5F);
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    ASSERT_EQ(worker.push(91U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto old_epoch=worker.sourceEpoch(91U);
    ASSERT_TRUE(old_epoch.has_value());
    for(int i=0;i<2000 && !gate->entered.load();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if(!gate->entered.load()) {
        gate->resume.store(true);
        FAIL() << "Old-epoch VAD did not enter the test gate";
        return;
    }
    worker.forget(91U);
    ASSERT_EQ(worker.push(91U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=worker.sourceEpoch(91U);
    ASSERT_TRUE(new_epoch.has_value());
    ASSERT_NE(*old_epoch,*new_epoch);
    ASSERT_EQ(worker.retire(91U),new_epoch);
    worker.closeInput();
    gate->fail_once.store(true);
    gate->resume.store(true);

    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int i=0;i<2000 && (!worker.finished() || events.size()<4U);++i) {
        auto batch=worker.drainAcknowledgements();
        for(auto& event:batch) events.push_back(std::move(event));
        if(!worker.finished() || events.size()<4U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(worker.finished());
    ASSERT_EQ(events.size(),4U);
    EXPECT_EQ(events[0].kind,Kind::Error);
    EXPECT_EQ(events[0].epoch,*old_epoch);
    EXPECT_EQ(events[0].error,"delayed stale-epoch VAD failure");
    EXPECT_EQ(events[1].kind,Kind::Forgot);
    EXPECT_GT(events[1].epoch,*old_epoch);
    EXPECT_LT(events[1].epoch,*new_epoch);
    EXPECT_EQ(events[2].kind,Kind::Bound);
    EXPECT_EQ(events[2].epoch,*new_epoch);
    EXPECT_EQ(events[3].kind,Kind::Retired);
    EXPECT_EQ(events[3].epoch,*new_epoch);
    ASSERT_TRUE(events[2].generation.has_value());
    EXPECT_EQ(events[3].generation,events[2].generation);
    EXPECT_EQ(worker.inputStatistics().invalidated_chunks,0U);
    EXPECT_EQ(worker.inputStatistics().outstanding_source_slots,0U);
    std::vector<SourceSpeechResult> results;
    for(int i=0;i<2000 && results.empty();++i) {
        results=speech.takeResults();
        if(results.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(results.size(),1U);
    EXPECT_EQ(results[0].track_id,91U);
    EXPECT_EQ(results[0].generation,*events[2].generation);
    EXPECT_EQ(results[0].transcript.text,"4");
    speech.releaseRetired(results[0].generation);
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


TEST(AsyncSpeechIngressWorker, TimestampOverflowDropsQueuedPcm) {
    auto gate=std::make_shared<PausedVadGate>();
    PausedVad vad{gate};
    auto cfg=speechOptions();
    cfg.max_segment_samples=4U;
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),cfg};
    SpeechPcmIngressOptions ingress_cfg{};
    ingress_cfg.max_sources=1U;
    ingress_cfg.max_chunk_samples=4U;
    AsyncSpeechIngressWorker worker{speech,ingress_cfg};
    const std::vector<float> pcm(4U,0.5F);
    const Timestamp near_limit{std::numeric_limits<std::int64_t>::max()-1,
                               {ClockDomain::Monotonic,17U}};
    ASSERT_EQ(worker.push(71U,audioBlock(pcm,near_limit)),
              SpeechPcmAdmission::Accepted);
    const auto first_epoch=worker.sourceEpoch(71U);
    ASSERT_TRUE(first_epoch.has_value());
    for(int i=0;i<2000 && !gate->entered.load();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if(!gate->entered.load()) {
        gate->resume.store(true);
        FAIL() << "VAD did not enter the test gate";
        return;
    }
    EXPECT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    gate->resume.store(true);
    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int i=0;i<2000 && events.size()<2U;++i) {
        auto batch=worker.drainAcknowledgements();
        for(auto& e:batch)events.push_back(std::move(e));
        if(events.size()<2U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    ASSERT_EQ(events.size(),2U);
    EXPECT_EQ(events[0].kind,Kind::Error);
    EXPECT_EQ(events[0].epoch,*first_epoch);
    EXPECT_EQ(events[1].kind,Kind::Forgot);
    EXPECT_NE(events[1].epoch,*first_epoch);
    EXPECT_EQ(worker.inputStatistics().invalidated_chunks,1U);
    EXPECT_EQ(worker.inputStatistics().outstanding_source_slots,0U);
    ASSERT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=worker.sourceEpoch(71U);
    ASSERT_TRUE(new_epoch.has_value());
    worker.closeInput();
    for(int i=0;i<2000 && !worker.finished();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    ASSERT_TRUE(worker.finished());
    const auto resumed=worker.drainAcknowledgements();
    ASSERT_EQ(resumed.size(),1U);
    EXPECT_EQ(resumed[0].kind,Kind::Bound);
    EXPECT_EQ(resumed[0].epoch,*new_epoch);
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

TEST(AsyncSpeechIngressWorker, VadFailureOfOneSourcePreservesOtherSourceAsr) {
    FailingSecondVad vad{};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    AsyncSpeechIngressWorker worker{speech};
    const std::vector<float> pcm(4U,0.5F);
    ASSERT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto failing_epoch=worker.sourceEpoch(71U);
    ASSERT_TRUE(failing_epoch.has_value());
    ASSERT_EQ(worker.push(71U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    ASSERT_EQ(worker.push(72U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto healthy_epoch=worker.sourceEpoch(72U);
    ASSERT_TRUE(healthy_epoch.has_value());
    ASSERT_EQ(worker.retire(72U),healthy_epoch);
    worker.closeInput();

    for(int i=0;i<2000 && !worker.finished();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    ASSERT_TRUE(worker.finished());
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    const auto events=worker.drainAcknowledgements();
    std::optional<std::uint64_t> healthy_generation;
    bool saw_error=false, saw_forget=false, saw_retire=false;
    for(const auto& event:events) {
        if(event.source_id==71U) {
            if(event.kind==Kind::Error) {
                EXPECT_EQ(event.epoch,*failing_epoch);
                EXPECT_EQ(event.error,"synthetic VAD failure");
                saw_error=true;
            } else if(event.kind==Kind::Forgot) {
                EXPECT_NE(event.epoch,*failing_epoch);
                saw_forget=true;
            } else {
                EXPECT_EQ(event.kind,Kind::Bound);
            }
        } else {
            EXPECT_EQ(event.source_id,72U);
            EXPECT_EQ(event.epoch,*healthy_epoch);
            if(event.kind==Kind::Bound) {
                ASSERT_TRUE(event.generation.has_value());
                healthy_generation=event.generation;
            } else if(event.kind==Kind::Retired) {
                ASSERT_TRUE(event.generation.has_value());
                if(healthy_generation) {
                    EXPECT_EQ(event.generation,healthy_generation);
                }
                saw_retire=true;
            } else {
                FAIL() << "Healthy source received error or invalidation";
            }
        }
    }
    EXPECT_TRUE(saw_error);
    EXPECT_TRUE(saw_forget);
    EXPECT_TRUE(saw_retire);
    ASSERT_TRUE(healthy_generation.has_value());
    EXPECT_FALSE(speech.activeGeneration(71U).has_value());

    std::vector<SourceSpeechResult> results;
    for(int i=0;i<2000 && results.empty();++i) {
        results=speech.takeResults();
        if(results.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(results.size(),1U);
    EXPECT_EQ(results[0].track_id,72U);
    EXPECT_EQ(results[0].generation,*healthy_generation);
    EXPECT_EQ(results[0].transcript.text,"4");
    EXPECT_EQ(results[0].audio_start_sample,0U);
    EXPECT_EQ(results[0].audio_end_sample,4U);
    EXPECT_TRUE(speech.takeResults().empty());
    speech.releaseRetired(*healthy_generation);
}

TEST(AsyncSpeechIngressWorker, RetirementCapacityReportsDiscardWithoutLosingPriorResult) {
    WorkerVad vad{};
    auto options=speechOptions();
    options.max_retired_generations=1U;
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),options};
    AsyncSpeechIngressWorker worker{speech};
    const std::vector<float> pcm(4U,0.5F);
    using Ack=AsyncSpeechIngressWorker::Acknowledgement;
    using Kind=Ack::Kind;
    auto drainUntil=[&](std::size_t expected) {
        std::vector<Ack> events;
        for(int i=0;i<2000 && events.size()<expected;++i) {
            auto batch=worker.drainAcknowledgements();
            for(auto& event:batch) events.push_back(std::move(event));
            if(events.size()<expected)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return events;
    };

    ASSERT_EQ(worker.push(301U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto first_epoch=worker.sourceEpoch(301U);
    ASSERT_TRUE(first_epoch.has_value());
    ASSERT_EQ(worker.retire(301U),first_epoch);
    const auto first=drainUntil(2U);
    ASSERT_EQ(first.size(),2U);
    EXPECT_EQ(first[0].kind,Kind::Bound);
    EXPECT_EQ(first[1].kind,Kind::Retired);
    ASSERT_TRUE(first[0].generation.has_value());
    EXPECT_EQ(first[0].generation,first[1].generation);
    EXPECT_EQ(speech.statistics().retired_generations,1U);

    ASSERT_EQ(worker.push(302U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto second_epoch=worker.sourceEpoch(302U);
    ASSERT_TRUE(second_epoch.has_value());
    ASSERT_EQ(worker.retire(302U),second_epoch);
    worker.closeInput();
    const auto second=drainUntil(3U);
    ASSERT_EQ(second.size(),3U);
    EXPECT_EQ(second[0].kind,Kind::Bound);
    EXPECT_EQ(second[1].kind,Kind::Error);
    EXPECT_EQ(second[2].kind,Kind::Retired);
    for(const auto& event:second) EXPECT_EQ(event.epoch,*second_epoch);
    EXPECT_EQ(second[1].error,
              "Speech retirement capacity exhausted; final utterance discarded");
    EXPECT_FALSE(second[1].generation.has_value());
    EXPECT_FALSE(second[2].generation.has_value());
    EXPECT_FALSE(speech.activeGeneration(302U).has_value());
    EXPECT_EQ(speech.statistics().retired_generations,1U);

    std::vector<SourceSpeechResult> results;
    for(int i=0;i<2000 && results.empty();++i) {
        results=speech.takeResults();
        if(results.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(results.size(),1U);
    EXPECT_EQ(results[0].generation,*first[0].generation);
    EXPECT_EQ(results[0].track_id,301U);
    EXPECT_EQ(results[0].transcript.text,"4");
    speech.releaseRetired(*first[0].generation);
}

TEST(AsyncSpeechIngressWorker, ReusedIdRetirementKeepsCaptureClock) {
    WorkerVad vad{};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    AsyncSpeechIngressWorker worker{speech};
    const std::vector<float> pcm(4U,0.5F);
    const Timestamp t1{1000000000LL,{ClockDomain::Monotonic,17U}};
    const Timestamp t2{3000000000LL,{ClockDomain::Monotonic,17U}};
    using Ack=AsyncSpeechIngressWorker::Acknowledgement;
    std::vector<Ack> acks;
    auto collect=[&](std::size_t count) {
        for(int i=0;i<2000 && acks.size()<count;++i) {
            auto batch=worker.drainAcknowledgements();
            for(auto& a:batch) acks.push_back(std::move(a));
            if(acks.size()<count)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return acks.size()==count;
    };
    ASSERT_EQ(worker.push(71U,audioBlock(pcm,t1)),SpeechPcmAdmission::Accepted);
    const auto epoch1=worker.sourceEpoch(71U);
    ASSERT_TRUE(epoch1.has_value());
    ASSERT_EQ(worker.retire(71U),epoch1);
    ASSERT_TRUE(collect(2U));
    ASSERT_EQ(acks[1].kind,Ack::Kind::Retired);
    ASSERT_EQ(worker.push(71U,audioBlock(pcm,t2)),SpeechPcmAdmission::Accepted);
    const auto epoch2=worker.sourceEpoch(71U);
    ASSERT_TRUE(epoch2.has_value());
    EXPECT_NE(epoch1,epoch2);
    ASSERT_EQ(worker.retire(71U),epoch2);
    worker.closeInput();
    ASSERT_TRUE(collect(4U));
    ASSERT_EQ(acks[0].kind,Ack::Kind::Bound);
    ASSERT_EQ(acks[2].kind,Ack::Kind::Bound);
    ASSERT_EQ(acks[3].kind,Ack::Kind::Retired);
    ASSERT_TRUE(acks[0].generation.has_value());
    ASSERT_TRUE(acks[2].generation.has_value());
    EXPECT_NE(acks[0].generation,acks[2].generation);
    EXPECT_EQ(acks[0].epoch,*epoch1);
    EXPECT_EQ(acks[1].epoch,*epoch1);
    EXPECT_EQ(acks[2].epoch,*epoch2);
    EXPECT_EQ(acks[3].epoch,*epoch2);
    std::vector<SourceSpeechResult> results;
    for(int i=0;i<2000 && results.size()<2U;++i) {
        auto batch=speech.takeResults();
        for(auto& result:batch) results.push_back(std::move(result));
        if(results.size()<2U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(results.size(),2U);
    for(const auto& result:results) {
        const bool first=result.generation==*acks[0].generation;
        EXPECT_TRUE(first || result.generation==*acks[2].generation);
        EXPECT_EQ(result.start_time.clock(),first?t1.clock():t2.clock());
        EXPECT_EQ(result.start_time.nanoseconds(),
                  first?t1.nanoseconds():t2.nanoseconds());
        EXPECT_EQ(result.track_id,71U);
        speech.releaseRetired(result.generation);
    }
}

TEST(AsyncSpeechIngressWorker, RuntimeCapacityMismatchReportsError) {
    WorkerVad vad{};
    auto cfg=speechOptions();
    cfg.max_sources=1U;
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),cfg};
    SpeechPcmIngressOptions ingress{};
    ingress.max_sources=2U;
    AsyncSpeechIngressWorker worker{speech,ingress};
    const std::vector<float> pcm(4U,0.5F);
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    ASSERT_EQ(worker.push(11U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int i=0;i<2000 && events.empty();++i) {
        events=worker.drainAcknowledgements();
        if(events.empty()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(events.size(),1U);
    EXPECT_EQ(events[0].kind,Kind::Bound);
    ASSERT_EQ(worker.push(12U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto rejected_epoch=worker.sourceEpoch(12U);
    ASSERT_TRUE(rejected_epoch.has_value());
    events.clear();
    for(int i=0;i<2000 && events.size()<2U;++i) {
        auto batch=worker.drainAcknowledgements();
        for(auto& event:batch) events.push_back(std::move(event));
        if(events.size()<2U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(events.size(),2U);
    EXPECT_EQ(events[0].kind,Kind::Error);
    EXPECT_EQ(events[0].epoch,*rejected_epoch);
    EXPECT_EQ(events[0].error,"Speech runtime source capacity exhausted");
    EXPECT_EQ(events[1].kind,Kind::Forgot);
    EXPECT_NE(events[1].epoch,*rejected_epoch);
    EXPECT_FALSE(speech.activeGeneration(12U).has_value());
    EXPECT_TRUE(speech.activeGeneration(11U).has_value());
    EXPECT_EQ(speech.trackStatus(11U).received_samples,4U);
    EXPECT_FALSE(speech.trackStatus(11U).error.has_value());
    EXPECT_EQ(worker.inputStatistics().outstanding_source_slots,1U);
}

TEST(AsyncSpeechIngressWorker, ForgetWhileVadBlockedInvalidatesOldEpochBeforeReuse) {
    auto gate=std::make_shared<PausedVadGate>();
    PausedVad vad{gate};
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),speechOptions()};
    AsyncSpeechIngressWorker worker{speech};
    const std::vector<float> pcm(4U,0.5F);
    ASSERT_EQ(worker.push(81U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto old_epoch=worker.sourceEpoch(81U);
    ASSERT_TRUE(old_epoch.has_value());
    for(int i=0;i<2000 && !gate->entered.load();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if(!gate->entered.load()) {
        gate->resume.store(true);
        FAIL() << "VAD did not enter the test gate";
        return;
    }
    // A queued old chunk must not survive explicit cancellation.
    ASSERT_EQ(worker.push(81U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    worker.forget(81U);
    ASSERT_EQ(worker.push(81U,audioBlock(pcm)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=worker.sourceEpoch(81U);
    ASSERT_TRUE(new_epoch.has_value());
    EXPECT_NE(*old_epoch,*new_epoch);
    ASSERT_EQ(worker.retire(81U),new_epoch);
    worker.closeInput();
    gate->resume.store(true);
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int i=0;i<2000 && (!worker.finished() || events.size()<4U);++i) {
        auto batch=worker.drainAcknowledgements();
        for(auto& e:batch) events.push_back(std::move(e));
        if(!worker.finished() || events.size()<4U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(worker.finished());
    ASSERT_EQ(events.size(),4U);
    EXPECT_EQ(events[0].kind,Kind::Bound);
    EXPECT_EQ(events[1].kind,Kind::Forgot);
    EXPECT_EQ(events[2].kind,Kind::Bound);
    EXPECT_EQ(events[3].kind,Kind::Retired);
    EXPECT_EQ(events[0].epoch,*old_epoch);
    EXPECT_EQ(events[2].epoch,*new_epoch);
    EXPECT_EQ(events[3].epoch,*new_epoch);
    ASSERT_TRUE(events[0].generation.has_value());
    ASSERT_TRUE(events[2].generation.has_value());
    EXPECT_NE(events[0].generation,events[2].generation);
    std::vector<SourceSpeechResult> results;
    for(int i=0;i<2000 && results.empty();++i) {
        results=speech.takeResults();
        if(results.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(results.size(),1U);
    EXPECT_EQ(results[0].generation,*events[2].generation);
    EXPECT_EQ(results[0].track_id,81U);
    EXPECT_EQ(results[0].transcript.text,"4");
    speech.releaseRetired(results[0].generation);
}

namespace {
struct PausedAsrGate {
    std::atomic<bool> entered{false};
    std::atomic<bool> resume{false};
};
class PausedFirstAsr final : public IAsrEngine {
public:
    explicit PausedFirstAsr(std::shared_ptr<PausedAsrGate> gate)
        : gate_(std::move(gate)) {}
    BackendInfo backendInfo() const override { return {"paused-first-asr","1"}; }
    AsrCapabilities capabilities() const override { return {}; }
    Transcript transcribe(const SpeechSegment& segment) const override {
        const auto invocation=calls_.fetch_add(1);
        if(invocation==0) {
            gate_->entered.store(true);
            while(!gate_->resume.load())
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        Transcript transcript{};
        transcript.segment_id=segment.segment_id;
        transcript.text=std::to_string(invocation);
        return transcript;
    }
private:
    std::shared_ptr<PausedAsrGate> gate_;
    mutable std::atomic<unsigned> calls_{0U};
};
} // namespace

TEST(AsyncSpeechIngressWorker, InFlightAsrForgetDoesNotPublishIntoReusedSource) {
    auto gate=std::make_shared<PausedAsrGate>();
    WorkerVad vad{};
    PerSourceSpeechRuntime speech{vad,std::make_shared<PausedFirstAsr>(gate),
                                  speechOptions()};
    AsyncSpeechIngressWorker worker{speech};
    const std::vector<float> voice(4U,0.5F), silence(4U,0.0F);
    ASSERT_EQ(worker.push(82U,audioBlock(voice)),SpeechPcmAdmission::Accepted);
    ASSERT_EQ(worker.push(82U,audioBlock(silence)),SpeechPcmAdmission::Accepted);
    const auto old_epoch=worker.sourceEpoch(82U);
    ASSERT_TRUE(old_epoch.has_value());
    for(int i=0;i<2000 && !gate->entered.load();++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    if(!gate->entered.load()) {
        gate->resume.store(true);
        FAIL() << "First ASR did not enter the test gate";
        return;
    }
    worker.forget(82U);
    ASSERT_EQ(worker.push(82U,audioBlock(voice)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=worker.sourceEpoch(82U);
    ASSERT_TRUE(new_epoch.has_value());
    EXPECT_NE(*old_epoch,*new_epoch);
    ASSERT_EQ(worker.retire(82U),new_epoch);
    worker.closeInput();
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int i=0;i<2000 && (!worker.finished() || events.size()<4U);++i) {
        auto batch=worker.drainAcknowledgements();
        for(auto& event:batch) events.push_back(std::move(event));
        if(!worker.finished() || events.size()<4U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    gate->resume.store(true);
    ASSERT_TRUE(worker.finished());
    ASSERT_EQ(events.size(),4U);
    EXPECT_EQ(events[0].kind,Kind::Bound);
    EXPECT_EQ(events[1].kind,Kind::Forgot);
    EXPECT_EQ(events[2].kind,Kind::Bound);
    EXPECT_EQ(events[3].kind,Kind::Retired);
    EXPECT_EQ(events[0].epoch,*old_epoch);
    // Forget has its own invalidation epoch between the old and reused source epochs.
    EXPECT_GT(events[1].epoch,*old_epoch);
    EXPECT_LT(events[1].epoch,*new_epoch);
    EXPECT_EQ(events[2].epoch,*new_epoch);
    EXPECT_EQ(events[3].epoch,*new_epoch);
    ASSERT_TRUE(events[0].generation.has_value());
    ASSERT_TRUE(events[2].generation.has_value());
    EXPECT_NE(events[0].generation,events[2].generation);
    std::vector<SourceSpeechResult> results;
    for(int i=0;i<2000 && results.empty();++i) {
        results=speech.takeResults();
        if(results.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(results.size(),1U);
    EXPECT_EQ(results[0].track_id,82U);
    EXPECT_EQ(results[0].generation,*events[2].generation);
    EXPECT_EQ(results[0].transcript.text,"1");
    EXPECT_TRUE(speech.takeResults().empty());
    speech.releaseRetired(results[0].generation);
}

TEST(AsyncSpeechIngressWorker, FixedVadFramesPreserveSourceLocalSampleOffsets) {
    // Cross-check sibling PR #54's fixed 10 ms framing contract.
    WorkerVad vad{};
    auto options=speechOptions();
    options.min_speech_samples=160U;
    options.pre_roll_samples=160U;
    options.post_roll_samples=320U;
    PerSourceSpeechRuntime speech{vad,std::make_shared<WorkerAsr>(),options};
    AsyncSpeechIngressWorker worker{speech};
    const ClockIdentity clock{ClockDomain::Monotonic,17U};
    const std::vector<float> quiet(160U,0.0F), voiced(160U,0.5F);
    for(std::int64_t frame=0;frame<9;++frame) {
        const auto& pcm=(frame>=2 && frame<5)?voiced:quiet;
        const Timestamp capture{1000000000LL+frame*10000000LL,clock};
        ASSERT_EQ(worker.push(83U,audioBlock(pcm,capture)),
                  SpeechPcmAdmission::Accepted);
    }
    const auto epoch=worker.sourceEpoch(83U);
    ASSERT_TRUE(epoch.has_value());
    ASSERT_EQ(worker.retire(83U),epoch);
    worker.closeInput();
    std::vector<AsyncSpeechIngressWorker::Acknowledgement> events;
    for(int i=0;i<2000 && (!worker.finished() || events.size()<2U);++i) {
        auto batch=worker.drainAcknowledgements();
        for(auto& event:batch) events.push_back(std::move(event));
        if(!worker.finished() || events.size()<2U)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(worker.finished());
    ASSERT_EQ(events.size(),2U);
    using Kind=AsyncSpeechIngressWorker::Acknowledgement::Kind;
    EXPECT_EQ(events[0].kind,Kind::Bound);
    EXPECT_EQ(events[1].kind,Kind::Retired);
    EXPECT_EQ(events[0].epoch,*epoch);
    EXPECT_EQ(events[1].epoch,*epoch);
    ASSERT_TRUE(events[0].generation.has_value());
    EXPECT_EQ(events[1].generation,events[0].generation);
    std::vector<SourceSpeechResult> results;
    for(int i=0;i<2000 && results.empty();++i) {
        results=speech.takeResults();
        if(results.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(results.size(),1U);
    EXPECT_EQ(results[0].track_id,83U);
    EXPECT_EQ(results[0].generation,*events[0].generation);
    // [160,1120) source samples at 16 kHz => [1.01,1.07) seconds.
    EXPECT_EQ(results[0].transcript.text,"960");
    EXPECT_EQ(results[0].audio_start_sample,160U);
    EXPECT_EQ(results[0].audio_end_sample,1120U);
    EXPECT_EQ(results[0].audio_end_sample-results[0].audio_start_sample,960U);
    EXPECT_EQ(results[0].start_time.clock(),clock);
    EXPECT_EQ(results[0].end_time.clock(),clock);
    EXPECT_EQ(results[0].start_time.nanoseconds(),1010000000LL);
    EXPECT_EQ(results[0].end_time.nanoseconds(),1070000000LL);
    speech.releaseRetired(results[0].generation);
}


TEST(AsyncSpeechIngressWorker, DelayedForgetPreservesReusedEpochAndWavAnnotations) {
    const auto root=std::filesystem::temp_directory_path() /
        ("audition-forget-epoch-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto old_wav=root/"old.wav";
    const auto new_wav=root/"new.wav";
    const Timestamp origin{1000000000LL,{ClockDomain::Monotonic,17U}};
    recording::SourceRecordingSession recordings{};
    recordings.begin(71U,1001U,old_wav,16000U,origin);
    ASSERT_EQ(recordings.retireDeferred(71U),1001U);
    recordings.begin(71U,1002U,new_wav,16000U,origin);

    demo::SpeechEpochRecordingMap epochs{
        {{71U,7U},1001U}, // old deferred WAV
        {{71U,8U},1002U}, // reused source already bound
        {{71U,9U},1003U}, // later epoch must also survive
        {{72U,2U},2001U}  // unrelated source must survive
    };
    std::set<RecordingId> deferred{1001U};
    // Bound(new) may be delivered before delayed Forgot(old): the
    // acknowledgement's epoch is the first valid new epoch, not the old one.
    recordings.bindRecordingAsr(1002U,88U);
    demo::clearForgottenSpeechRecordingEpochs(
        epochs,deferred,recordings,71U,8U);
    EXPECT_EQ(epochs.count({71U,7U}),0U);
    EXPECT_EQ(epochs.at({71U,8U}),1002U);
    EXPECT_EQ(epochs.at({71U,9U}),1003U);
    EXPECT_EQ(epochs.at({72U,2U}),2001U);
    EXPECT_TRUE(deferred.empty());
    EXPECT_TRUE(recordings.active(71U));
    EXPECT_EQ(recordings.boundAsrGenerations(),1U);

    SourceSpeechResult result{};
    result.track_id=71U;
    result.generation=88U;
    result.transcript.text="reused-source";
    result.start_time=origin;
    result.end_time=Timestamp{1010000000LL,origin.clock()};
    recordings.addSpeech(result);
    recordings.retire(71U,88U);
    recordings.completeGeneration(88U);
    const auto old_saved=readRecordingTimelineJson(
        std::filesystem::path{old_wav.string()+".json"});
    const auto new_saved=readRecordingTimelineJson(
        std::filesystem::path{new_wav.string()+".json"});
    EXPECT_TRUE(old_saved.speech().empty());
    ASSERT_EQ(new_saved.speech().size(),1U);
    EXPECT_EQ(new_saved.speech().front().text,"reused-source");
    EXPECT_EQ(recordings.retainedRecordings(),0U);
    std::filesystem::remove_all(root);
}
