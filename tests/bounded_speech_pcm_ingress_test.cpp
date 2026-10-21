#include <audition/speech/bounded_speech_pcm_ingress.hpp>
#include <audition/speech/per_source_speech_runtime.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <limits>
#include <thread>
#include <memory>
#include <string>
#include <vector>

using namespace audition;
namespace {
AudioView block(const std::vector<float>& v, std::uint64_t seq = 0U) {
    return AudioView{v.data(),v.size(),{16000U,1U,AudioLayout::Interleaved},
                     Timestamp{},seq};
}
} // namespace

TEST(BoundedSpeechPcmIngress, DeliversOrderedOwnedAudioAndPreservesOtherSources) {
    SpeechPcmIngressOptions limits{};
    limits.max_pending_chunks=4U;
    limits.max_pending_samples=32U;
    limits.max_chunk_samples=8U;
    BoundedSpeechPcmIngress ingress{limits};
    std::vector<float> a(8U,0.3F),b(6U,0.7F);
    EXPECT_EQ(ingress.push(1U,block(a,31U)),SpeechPcmAdmission::Accepted);
    EXPECT_EQ(ingress.push(2U,block(b,32U)),SpeechPcmAdmission::Accepted);
    a.assign(8U,0.9F); // The queue must own the earlier values.
    auto first=ingress.tryPop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->kind,SpeechPcmIngressEvent::Kind::Audio);
    EXPECT_EQ(first->source_id,1U);
    EXPECT_EQ(first->audio.samples()[0],0.3F);
    EXPECT_EQ(first->audio.sequenceNumber(),31U);
    auto second=ingress.tryPop();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->source_id,2U);
    EXPECT_EQ(second->audio.samples().size(),6U);
    EXPECT_FALSE(ingress.tryPop().has_value());
    EXPECT_EQ(ingress.statistics().pending_samples,0U);
}

TEST(BoundedSpeechPcmIngress, OverflowInvalidatesOnlyAffectedSourceBeforeReuse) {
    SpeechPcmIngressOptions limits{};
    limits.max_pending_chunks=3U;
    limits.max_pending_samples=16U;
    limits.max_chunk_samples=8U;
    BoundedSpeechPcmIngress ingress{limits};
    const std::vector<float> audio(8U,0.3F);
    EXPECT_EQ(ingress.push(10U,block(audio)),SpeechPcmAdmission::Accepted);
    EXPECT_EQ(ingress.push(11U,block(audio)),SpeechPcmAdmission::Accepted);
    const auto denied=ingress.push(10U,block(audio));
    EXPECT_EQ(denied,SpeechPcmAdmission::RejectedCapacity);
    const auto stats=ingress.statistics();
    EXPECT_EQ(stats.pending_chunks,1U);
    EXPECT_EQ(stats.pending_samples,8U);
    EXPECT_EQ(stats.pending_invalidations,1U);
    EXPECT_EQ(stats.invalidated_chunks,1U);
    EXPECT_EQ(ingress.push(10U,block(audio)),SpeechPcmAdmission::Accepted);
    auto forget=ingress.tryPop();
    ASSERT_TRUE(forget.has_value());
    EXPECT_EQ(forget->kind,SpeechPcmIngressEvent::Kind::Forget);
    EXPECT_EQ(forget->source_id,10U);
    auto survivor=ingress.tryPop();
    ASSERT_TRUE(survivor.has_value());
    EXPECT_EQ(survivor->source_id,11U);
    auto replacement=ingress.tryPop();
    ASSERT_TRUE(replacement.has_value());
    EXPECT_EQ(replacement->source_id,10U);
    EXPECT_EQ(replacement->epoch,forget->epoch);
    EXPECT_FALSE(ingress.tryPop().has_value());
}

TEST(BoundedSpeechPcmIngress, ExplicitForgetReusesSourceWithFreshEpoch) {
    BoundedSpeechPcmIngress ingress{};
    const std::vector<float> audio(4U,0.3F);
    EXPECT_EQ(ingress.push(42U,block(audio)),SpeechPcmAdmission::Accepted);
    auto original=ingress.tryPop();
    ASSERT_TRUE(original.has_value());
    const auto previous=original->epoch;
    ingress.forget(42U);
    EXPECT_EQ(ingress.push(42U,block(audio)),SpeechPcmAdmission::Accepted);
    auto event=ingress.tryPop();
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->kind,SpeechPcmIngressEvent::Kind::Forget);
    EXPECT_GT(event->epoch,previous);
    auto fresh=ingress.tryPop();
    ASSERT_TRUE(fresh.has_value());
    EXPECT_EQ(fresh->source_id,42U);
    EXPECT_GT(fresh->epoch,previous);
    EXPECT_FALSE(ingress.tryPop().has_value());
}

TEST(BoundedSpeechPcmIngress, RejectionIsTransactionalAndCapacityRecovers) {
    SpeechPcmIngressOptions limits{};
    limits.max_sources=1U;
    limits.max_pending_chunks=1U;
    limits.max_pending_samples=4U;
    limits.max_chunk_samples=4U;
    BoundedSpeechPcmIngress ingress{limits};
    const std::vector<float> pcm(4U,0.1F);
    const std::vector<float> longer(5U,0.1F);
    EXPECT_THROW(ingress.push(1U,block(longer)),std::invalid_argument);
    const std::vector<float> nan={std::numeric_limits<float>::quiet_NaN()};
    EXPECT_THROW(ingress.push(1U,block(nan)),std::invalid_argument);
    EXPECT_EQ(ingress.statistics().sources,0U);
    EXPECT_EQ(ingress.push(1U,block(pcm)),SpeechPcmAdmission::Accepted);
    EXPECT_EQ(ingress.push(2U,block(pcm)),SpeechPcmAdmission::RejectedSourceLimit);
    EXPECT_EQ(ingress.push(1U,block(pcm)),SpeechPcmAdmission::RejectedCapacity);
    ingress.forget(1U);
    // A pending Forget reserves its source slot until the worker gets it.
    EXPECT_EQ(ingress.push(2U,block(pcm)),SpeechPcmAdmission::RejectedSourceLimit);
    EXPECT_EQ(ingress.statistics().outstanding_source_slots,1U);
    auto reset=ingress.tryPop();
    ASSERT_TRUE(reset.has_value());
    EXPECT_EQ(reset->kind,SpeechPcmIngressEvent::Kind::Forget);
    EXPECT_EQ(ingress.push(2U,block(pcm)),SpeechPcmAdmission::Accepted);
    auto a=ingress.tryPop();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->source_id,2U);
}

TEST(BoundedSpeechPcmIngress, BlockingConsumerWakesOnCloseAndDrains) {
    BoundedSpeechPcmIngress ingress{};
    const std::vector<float> audio(4U,0.2F);
    EXPECT_EQ(ingress.push(7U,block(audio)),SpeechPcmAdmission::Accepted);
    ingress.close();
    const auto first=ingress.waitPop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->source_id,7U);
    EXPECT_FALSE(ingress.waitPop().has_value());
    EXPECT_EQ(ingress.push(8U,block(audio)),SpeechPcmAdmission::Closed);

    BoundedSpeechPcmIngress waiting{};
    auto result=std::async(std::launch::async,[&]{return waiting.waitPop();});
    waiting.close();
    EXPECT_EQ(result.wait_for(std::chrono::seconds{2}),std::future_status::ready);
    EXPECT_FALSE(result.get().has_value());
}

TEST(BoundedSpeechPcmIngress, EmptyInputRespectsCloseWithoutMutatingState) {
    BoundedSpeechPcmIngress ingress{};
    const std::vector<float> empty{};
    EXPECT_EQ(ingress.push(1U,block(empty)),SpeechPcmAdmission::Accepted);
    const auto before=ingress.statistics();
    EXPECT_EQ(before.sources,0U);
    EXPECT_EQ(before.accepted_chunks,0U);
    ingress.close();
    EXPECT_EQ(ingress.push(1U,block(empty)),SpeechPcmAdmission::Closed);
    EXPECT_EQ(ingress.push(2U,block(empty)),SpeechPcmAdmission::Closed);
    const auto after=ingress.statistics();
    EXPECT_EQ(after.sources,before.sources);
    EXPECT_EQ(after.pending_chunks,before.pending_chunks);
    EXPECT_EQ(after.accepted_chunks,before.accepted_chunks);
    EXPECT_EQ(after.rejected_chunks,before.rejected_chunks);
    EXPECT_FALSE(ingress.waitPop().has_value());
}

TEST(BoundedSpeechPcmIngress, ConcurrentProducersPreservePerSourceAudioBeforeRetire) {
    SpeechPcmIngressOptions options{};
    options.max_sources=2U;
    options.max_pending_chunks=64U;
    options.max_pending_samples=256U;
    options.max_chunk_samples=4U;
    BoundedSpeechPcmIngress ingress{options};
    const std::vector<float> audio(4U,0.3F);
    auto produce=[&](std::uint64_t source_id) {
        for (std::uint64_t seq=1U;seq<=32U;++seq)
            if (ingress.push(source_id,block(audio,seq))!=SpeechPcmAdmission::Accepted)
                return false;
        return ingress.retire(source_id);
    };
    auto first=std::async(std::launch::async,produce,1U);
    auto second=std::async(std::launch::async,produce,2U);
    ASSERT_TRUE(first.get());
    ASSERT_TRUE(second.get());
    std::uint64_t next_sequence[2]={1U,1U};
    bool retired[2]={false,false};
    for (std::size_t i=0U;i<66U;++i) {
        const auto event=ingress.tryPop();
        ASSERT_TRUE(event.has_value());
        ASSERT_GE(event->source_id,1U);
        ASSERT_LE(event->source_id,2U);
        const auto index=static_cast<std::size_t>(event->source_id-1U);
        if (event->kind==SpeechPcmIngressEvent::Kind::Audio) {
            EXPECT_FALSE(retired[index]);
            EXPECT_EQ(event->audio.sequenceNumber(),next_sequence[index]++);
        } else {
            EXPECT_EQ(event->kind,SpeechPcmIngressEvent::Kind::Retire);
            EXPECT_FALSE(retired[index]);
            EXPECT_EQ(next_sequence[index],33U);
            retired[index]=true;
        }
    }
    EXPECT_EQ(next_sequence[0],33U);
    EXPECT_EQ(next_sequence[1],33U);
    EXPECT_TRUE(retired[0]);
    EXPECT_TRUE(retired[1]);
    EXPECT_FALSE(ingress.tryPop().has_value());
    EXPECT_EQ(ingress.statistics().outstanding_source_slots,0U);
}

TEST(BoundedSpeechPcmIngress, AtomicRetireReturnsExactlyEnqueuedSourceEpoch) {
    SpeechPcmIngressOptions options{};
    options.max_sources=1U;
    options.max_chunk_samples=4U;
    BoundedSpeechPcmIngress ingress{options};
    const std::vector<float> pcm(4U,0.25F);
    ASSERT_EQ(ingress.push(123U,block(pcm)),SpeechPcmAdmission::Accepted);
    const auto original_epoch=ingress.sourceEpoch(123U);
    ASSERT_TRUE(original_epoch.has_value());

    const auto retired_epoch=ingress.retireWithEpoch(123U);
    ASSERT_TRUE(retired_epoch.has_value());
    EXPECT_EQ(retired_epoch,original_epoch);
    EXPECT_FALSE(ingress.retireWithEpoch(123U).has_value());
    EXPECT_EQ(ingress.push(123U,block(pcm)),
              SpeechPcmAdmission::RejectedRetiring);

    const auto audio=ingress.tryPop();
    ASSERT_TRUE(audio.has_value());
    EXPECT_EQ(audio->kind,SpeechPcmIngressEvent::Kind::Audio);
    EXPECT_EQ(audio->epoch,*retired_epoch);
    const auto retirement=ingress.tryPop();
    ASSERT_TRUE(retirement.has_value());
    EXPECT_EQ(retirement->kind,SpeechPcmIngressEvent::Kind::Retire);
    EXPECT_EQ(retirement->epoch,*retired_epoch);
    EXPECT_FALSE(ingress.tryPop().has_value());

    ASSERT_EQ(ingress.push(123U,block(pcm)),SpeechPcmAdmission::Accepted);
    const auto reused_epoch=ingress.sourceEpoch(123U);
    ASSERT_TRUE(reused_epoch.has_value());
    EXPECT_GT(*reused_epoch,*retired_epoch);
    EXPECT_EQ(ingress.retireWithEpoch(123U),reused_epoch);
    ingress.close();
    EXPECT_FALSE(ingress.retireWithEpoch(123U).has_value());
}

TEST(BoundedSpeechPcmIngress, PendingForgetCannotAccumulateUnboundedDistinctIds) {
    SpeechPcmIngressOptions options{};
    options.max_sources=1U;
    BoundedSpeechPcmIngress ingress{options};
    const std::vector<float> audio(4U,0.25F);
    for(std::uint64_t i=1U;i<=48U;++i) {
        EXPECT_EQ(ingress.push(i,block(audio)),SpeechPcmAdmission::Accepted);
        ingress.forget(i);
        EXPECT_EQ(ingress.statistics().pending_invalidations,1U);
        EXPECT_EQ(ingress.statistics().outstanding_source_slots,1U);
        EXPECT_EQ(ingress.push(i+1000U,block(audio)),
                  SpeechPcmAdmission::RejectedSourceLimit);
        auto item=ingress.tryPop();
        ASSERT_TRUE(item.has_value());
        EXPECT_EQ(item->kind,SpeechPcmIngressEvent::Kind::Forget);
        EXPECT_EQ(item->source_id,i);
        EXPECT_FALSE(ingress.tryPop().has_value());
        EXPECT_EQ(ingress.statistics().outstanding_source_slots,0U);
    }
}


namespace {
class IngressVadSession final : public IVadSession {
public:
    void reset() override {}
    VadResult process(AudioView audio) override {
        bool speech=false;
        for(std::size_t i=0;i<audio.sampleCount();++i)
            speech=speech || audio.data()[i]>0.2F;
        return {std::nullopt,speech};
    }
};
class IngressVad final : public IVoiceActivityDetector {
public:
    BackendInfo backendInfo() const override { return {"ingress-vad","1"}; }
    AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<IVadSession> createSession() const override {
        return std::make_unique<IngressVadSession>();
    }
};
class IngressAsr final : public IAsrEngine {
public:
    BackendInfo backendInfo() const override { return {"ingress-asr","1"}; }
    AsrCapabilities capabilities() const override { return {}; }
    Transcript transcribe(const SpeechSegment& segment) const override {
        Transcript result{};
        result.segment_id=segment.segment_id;
        result.text=std::to_string(segment.audio.samples().size());
        return result;
    }
};
}

TEST(BoundedSpeechPcmIngress, WorkerForgetStopsSpeechAcrossPcmGap) {
    SpeechPcmIngressOptions limits{};
    limits.max_pending_chunks=1U;
    limits.max_pending_samples=4U;
    limits.max_chunk_samples=4U;
    BoundedSpeechPcmIngress ingress{limits};
    IngressVad vad{};
    PerSourceSpeechOptions options{};
    options.min_speech_samples=4U;
    options.pre_roll_samples=0U;
    options.post_roll_samples=0U;
    PerSourceSpeechRuntime runtime{vad,std::make_shared<IngressAsr>(),options};
    const std::vector<float> speech(4U,0.5F),quiet(4U,0.0F);

    const auto dispatch=[&]() {
        auto event=ingress.tryPop();
        if (!event) return false;
        if (event->kind==SpeechPcmIngressEvent::Kind::Forget)
            runtime.forget(event->source_id);
        else
            runtime.push(event->source_id,event->audio.view());
        return true;
    };
    EXPECT_EQ(ingress.push(87U,block(speech)),SpeechPcmAdmission::Accepted);
    ASSERT_TRUE(dispatch());
    const auto first=runtime.activeGeneration(87U);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(runtime.trackStatus(87U).buffered_samples,4U);
    EXPECT_EQ(runtime.trackStatus(87U).received_samples,4U);

    EXPECT_EQ(ingress.push(87U,block(speech)),SpeechPcmAdmission::Accepted);
    EXPECT_EQ(ingress.push(87U,block(speech)),SpeechPcmAdmission::RejectedCapacity);
    EXPECT_EQ(ingress.statistics().invalidated_chunks,1U);
    EXPECT_EQ(ingress.push(87U,block(speech)),SpeechPcmAdmission::Accepted);
    ASSERT_TRUE(dispatch()); // Prioritized Forget before post-gap audio.
    EXPECT_FALSE(runtime.activeGeneration(87U).has_value());
    ASSERT_TRUE(dispatch());
    const auto fresh=runtime.activeGeneration(87U);
    ASSERT_TRUE(fresh.has_value());
    EXPECT_NE(*fresh,*first);
    EXPECT_EQ(runtime.trackStatus(87U).received_samples,4U);
    EXPECT_EQ(runtime.trackStatus(87U).queued_segments,0U);
    EXPECT_EQ(ingress.push(87U,block(quiet)),SpeechPcmAdmission::Accepted);
    ASSERT_TRUE(dispatch());

    std::vector<SourceSpeechResult> received;
    for (int spin=0;spin<1000 && received.empty();++spin) {
        received=runtime.takeResults();
        if (received.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(received.size(),1U);
    EXPECT_EQ(received.front().generation,*fresh);
    EXPECT_EQ(received.front().audio_start_sample,0U);
    EXPECT_EQ(received.front().audio_end_sample,4U);
    EXPECT_EQ(runtime.trackStatus(87U).received_samples,8U);
    EXPECT_EQ(runtime.trackStatus(87U).queued_segments,1U);
    EXPECT_EQ(received.front().transcript.text,"4");
    EXPECT_EQ(runtime.statistics().completed_segments,1U);
}

TEST(BoundedSpeechPcmIngress, FifoRetirePreservesFinalUtteranceAndGeneration) {
    SpeechPcmIngressOptions limits{};
    limits.max_sources=2U;
    limits.max_pending_chunks=1U;
    limits.max_pending_samples=4U;
    limits.max_chunk_samples=4U;
    BoundedSpeechPcmIngress ingress{limits};
    IngressVad vad{};
    PerSourceSpeechOptions speech_options{};
    speech_options.min_speech_samples=4U;
    speech_options.pre_roll_samples=0U;
    speech_options.post_roll_samples=0U;
    PerSourceSpeechRuntime speech{vad,std::make_shared<IngressAsr>(),speech_options};
    const std::vector<float> voice(4U,0.5F);
    EXPECT_EQ(ingress.push(83U,block(voice)),SpeechPcmAdmission::Accepted);
    // Control must enqueue even at the audio-chunk capacity; processing
    // the command before the PCM would lose the final speech.
    ASSERT_TRUE(ingress.retire(83U));
    EXPECT_EQ(ingress.statistics().pending_chunks,1U);
    EXPECT_EQ(ingress.statistics().pending_retirements,1U);
    EXPECT_EQ(ingress.push(83U,block(voice)),
              SpeechPcmAdmission::RejectedRetiring);
    auto queued=ingress.tryPop();
    ASSERT_TRUE(queued.has_value());
    ASSERT_EQ(queued->kind,SpeechPcmIngressEvent::Kind::Audio);
    speech.push(queued->source_id,queued->audio.view());
    const auto active=speech.activeGeneration(83U);
    ASSERT_TRUE(active.has_value());
    auto control=ingress.tryPop();
    ASSERT_TRUE(control.has_value());
    EXPECT_EQ(control->kind,SpeechPcmIngressEvent::Kind::Retire);
    const auto retired=speech.retire(control->source_id);
    ASSERT_TRUE(retired.has_value());
    EXPECT_EQ(*retired,*active);
    EXPECT_FALSE(ingress.tryPop().has_value());
    EXPECT_EQ(ingress.statistics().pending_retirements,0U);

    std::vector<SourceSpeechResult> found;
    for (int spin=0;spin<1000 && found.empty();++spin) {
        found=speech.takeResults();
        if (found.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(found.size(),1U);
    EXPECT_EQ(found.front().transcript.text,"4");
    EXPECT_EQ(found.front().generation,*retired);
    const auto complete=speech.drainReady();
    ASSERT_EQ(complete.ready_retired_generations.size(),1U);
    EXPECT_EQ(complete.ready_retired_generations.front(),*retired);
    speech.releaseRetired(*retired);

    // Reusing an ODAS numeric ID after a finished retire must always
    // produce a new speech-runtime generation.
    EXPECT_EQ(ingress.push(83U,block(voice)),SpeechPcmAdmission::Accepted);
    auto reused=ingress.tryPop();
    ASSERT_TRUE(reused.has_value());
    EXPECT_NE(reused->epoch,queued->epoch);
    speech.push(reused->source_id,reused->audio.view());
    const auto fresh=speech.activeGeneration(83U);
    ASSERT_TRUE(fresh.has_value());
    EXPECT_NE(*fresh,*retired);
}

TEST(BoundedSpeechPcmIngress, RetireControlsAreBoundedAndBlockNewSources) {
    SpeechPcmIngressOptions cfg{};
    cfg.max_sources=1U;
    cfg.max_pending_chunks=1U;
    BoundedSpeechPcmIngress ingress{cfg};
    const std::vector<float> audio(4U,0.2F);
    for(std::uint64_t source=1U;source<=48U;++source) {
        ASSERT_EQ(ingress.push(source,block(audio)),SpeechPcmAdmission::Accepted);
        ASSERT_TRUE(ingress.retire(source));
        EXPECT_FALSE(ingress.retire(source));
        EXPECT_EQ(ingress.statistics().pending_retirements,1U);
        EXPECT_EQ(ingress.statistics().outstanding_source_slots,1U);
        EXPECT_EQ(ingress.push(source+1000U,block(audio)),
                  SpeechPcmAdmission::RejectedSourceLimit);
        const auto frame=ingress.tryPop();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->kind,SpeechPcmIngressEvent::Kind::Audio);
        const auto control=ingress.tryPop();
        ASSERT_TRUE(control.has_value());
        EXPECT_EQ(control->kind,SpeechPcmIngressEvent::Kind::Retire);
        EXPECT_EQ(control->source_id,source);
        EXPECT_FALSE(ingress.tryPop().has_value());
        EXPECT_EQ(ingress.statistics().outstanding_source_slots,0U);
    }
}

TEST(BoundedSpeechPcmIngress, StaleWorkerErrorCannotInvalidateReusedEpoch) {
    BoundedSpeechPcmIngress ingress{};
    const std::vector<float> pcm(4U,0.3F);
    ASSERT_EQ(ingress.push(61U,block(pcm)),SpeechPcmAdmission::Accepted);
    const auto old_epoch=ingress.sourceEpoch(61U);
    ASSERT_TRUE(old_epoch.has_value());
    const auto in_flight=ingress.tryPop();
    ASSERT_TRUE(in_flight.has_value());
    EXPECT_EQ(in_flight->epoch,*old_epoch);
    ingress.forget(61U);
    ASSERT_EQ(ingress.push(61U,block(pcm)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=ingress.sourceEpoch(61U);
    ASSERT_TRUE(new_epoch.has_value());
    ASSERT_NE(*old_epoch,*new_epoch);
    ASSERT_EQ(ingress.retireWithEpoch(61U),new_epoch);

    EXPECT_FALSE(ingress.forgetIfEpoch(61U,*old_epoch));
    EXPECT_EQ(ingress.statistics().pending_chunks,1U);
    EXPECT_EQ(ingress.statistics().pending_retirements,1U);
    EXPECT_EQ(ingress.statistics().invalidated_chunks,0U);
    auto invalidation=ingress.tryPop();
    ASSERT_TRUE(invalidation.has_value());
    EXPECT_EQ(invalidation->kind,SpeechPcmIngressEvent::Kind::Forget);
    auto new_audio=ingress.tryPop();
    ASSERT_TRUE(new_audio.has_value());
    EXPECT_EQ(new_audio->kind,SpeechPcmIngressEvent::Kind::Audio);
    EXPECT_EQ(new_audio->epoch,*new_epoch);
    auto retirement=ingress.tryPop();
    ASSERT_TRUE(retirement.has_value());
    EXPECT_EQ(retirement->kind,SpeechPcmIngressEvent::Kind::Retire);
    EXPECT_EQ(retirement->epoch,*new_epoch);
    EXPECT_FALSE(ingress.tryPop().has_value());

    ASSERT_EQ(ingress.push(61U,block(pcm)),SpeechPcmAdmission::Accepted);
    const auto current_epoch=ingress.sourceEpoch(61U);
    ASSERT_TRUE(current_epoch.has_value());
    EXPECT_TRUE(ingress.forgetIfEpoch(61U,*current_epoch));
    EXPECT_EQ(ingress.statistics().pending_chunks,0U);
    EXPECT_EQ(ingress.statistics().invalidated_chunks,1U);
    EXPECT_FALSE(ingress.sourceEpoch(61U).has_value());
    auto final_forget=ingress.tryPop();
    ASSERT_TRUE(final_forget.has_value());
    EXPECT_EQ(final_forget->kind,SpeechPcmIngressEvent::Kind::Forget);
    EXPECT_FALSE(ingress.tryPop().has_value());
}

TEST(BoundedSpeechPcmIngress, ExplicitForgetCancelsAnUnprocessedRetirement) {
    SpeechPcmIngressOptions cfg{};
    cfg.max_sources=1U;
    cfg.max_chunk_samples=4U;
    BoundedSpeechPcmIngress ingress{cfg};
    const std::vector<float> audio(4U,0.3F);
    ASSERT_EQ(ingress.push(55U,block(audio)),SpeechPcmAdmission::Accepted);
    ASSERT_TRUE(ingress.retire(55U));
    ingress.forget(55U);
    EXPECT_EQ(ingress.statistics().pending_retirements,0U);
    EXPECT_EQ(ingress.statistics().pending_chunks,0U);
    EXPECT_EQ(ingress.statistics().pending_invalidations,1U);
    EXPECT_EQ(ingress.statistics().invalidated_chunks,1U);
    auto control=ingress.tryPop();
    ASSERT_TRUE(control.has_value());
    EXPECT_EQ(control->kind,SpeechPcmIngressEvent::Kind::Forget);
    EXPECT_EQ(control->source_id,55U);
    EXPECT_FALSE(ingress.tryPop().has_value());
    EXPECT_EQ(ingress.push(56U,block(audio)),SpeechPcmAdmission::Accepted);
}

TEST(BoundedSpeechPcmIngress, RepeatedOverflowCoalescesForgetAndPreservesPeerAudio) {
    SpeechPcmIngressOptions limits{};
    limits.max_sources=2U;
    limits.max_pending_chunks=2U;
    limits.max_pending_samples=16U;
    limits.max_chunk_samples=8U;
    BoundedSpeechPcmIngress ingress{limits};
    const std::vector<float> audio(8U,0.3F);
    ASSERT_EQ(ingress.push(10U,block(audio,1U)),SpeechPcmAdmission::Accepted);
    ASSERT_EQ(ingress.push(11U,block(audio,2U)),SpeechPcmAdmission::Accepted);
    const auto old_epoch=ingress.sourceEpoch(10U);
    ASSERT_TRUE(old_epoch.has_value());
    EXPECT_EQ(ingress.push(10U,block(audio,3U)),
              SpeechPcmAdmission::RejectedCapacity);
    // The invalidated source can be admitted again, but the peer's
    // accepted chunk must survive repeated overload on the reused ID.
    ASSERT_EQ(ingress.push(10U,block(audio,4U)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=ingress.sourceEpoch(10U);
    ASSERT_TRUE(new_epoch.has_value());
    EXPECT_NE(*new_epoch,*old_epoch);
    EXPECT_EQ(ingress.push(10U,block(audio,5U)),
              SpeechPcmAdmission::RejectedCapacity);
    const auto stats=ingress.statistics();
    EXPECT_EQ(stats.pending_chunks,1U);
    EXPECT_EQ(stats.pending_samples,8U);
    EXPECT_EQ(stats.pending_invalidations,1U);
    EXPECT_EQ(stats.invalidated_chunks,2U);
    EXPECT_EQ(stats.rejected_chunks,2U);
    auto forgotten=ingress.tryPop();
    ASSERT_TRUE(forgotten.has_value());
    EXPECT_EQ(forgotten->kind,SpeechPcmIngressEvent::Kind::Forget);
    EXPECT_EQ(forgotten->source_id,10U);
    auto peer=ingress.tryPop();
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->kind,SpeechPcmIngressEvent::Kind::Audio);
    EXPECT_EQ(peer->source_id,11U);
    EXPECT_EQ(peer->audio.sequenceNumber(),2U);
    EXPECT_FALSE(ingress.tryPop().has_value());
    EXPECT_EQ(ingress.statistics().pending_samples,0U);
}

TEST(BoundedSpeechPcmIngress, RetiringSourceSlotRecoversWithoutInvalidatingNewSource) {
    SpeechPcmIngressOptions limits{};
    limits.max_sources=1U;
    limits.max_pending_chunks=2U;
    limits.max_pending_samples=16U;
    limits.max_chunk_samples=8U;
    BoundedSpeechPcmIngress ingress{limits};
    const std::vector<float> audio(8U,0.4F);
    ASSERT_EQ(ingress.push(70U,block(audio,1U)),SpeechPcmAdmission::Accepted);
    const auto retired_epoch=ingress.retireWithEpoch(70U);
    ASSERT_TRUE(retired_epoch.has_value());
    EXPECT_EQ(ingress.push(71U,block(audio,2U)),
              SpeechPcmAdmission::RejectedSourceLimit);
    EXPECT_EQ(ingress.statistics().pending_retirements,1U);
    auto old_audio=ingress.tryPop();
    ASSERT_TRUE(old_audio.has_value());
    EXPECT_EQ(old_audio->kind,SpeechPcmIngressEvent::Kind::Audio);
    EXPECT_EQ(old_audio->source_id,70U);
    auto retired=ingress.tryPop();
    ASSERT_TRUE(retired.has_value());
    EXPECT_EQ(retired->kind,SpeechPcmIngressEvent::Kind::Retire);
    EXPECT_EQ(retired->epoch,*retired_epoch);
    EXPECT_EQ(ingress.statistics().pending_retirements,0U);
    ASSERT_EQ(ingress.push(71U,block(audio,3U)),SpeechPcmAdmission::Accepted);
    const auto new_epoch=ingress.sourceEpoch(71U);
    ASSERT_TRUE(new_epoch.has_value());
    EXPECT_NE(*new_epoch,*retired_epoch);
    EXPECT_FALSE(ingress.forgetIfEpoch(70U,*retired_epoch));
    EXPECT_EQ(ingress.statistics().pending_chunks,1U);
    auto new_audio=ingress.tryPop();
    ASSERT_TRUE(new_audio.has_value());
    EXPECT_EQ(new_audio->kind,SpeechPcmIngressEvent::Kind::Audio);
    EXPECT_EQ(new_audio->source_id,71U);
    EXPECT_EQ(new_audio->epoch,*new_epoch);
    EXPECT_EQ(new_audio->audio.sequenceNumber(),3U);
    EXPECT_FALSE(ingress.tryPop().has_value());
}
