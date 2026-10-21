// Standalone consumer compiled against the installed C++17 package.
// No access to libaudition's source tree, GTest, Qt or model weights.
#include <audition/speech/per_source_speech_runtime.hpp>
#include <audition/speech/bounded_speech_pcm_ingress.hpp>
#include <audition/speech/async_speech_ingress_worker.hpp>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
class TestSession final : public audition::IVadSession {
public:
    void reset() override {}
    audition::VadResult process(audition::AudioView audio) override {
        bool active=false;
        for (std::size_t i=0;i<audio.sampleCount();++i)
            active = active || audio.data()[i]>0.2F;
        return {std::nullopt,active};
    }
};
class TestVad final : public audition::IVoiceActivityDetector {
public:
    audition::BackendInfo backendInfo() const override { return {"installed-vad","1"}; }
    audition::AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<audition::IVadSession> createSession() const override {
        return std::make_unique<TestSession>();
    }
};
class TestAsr final : public audition::IAsrEngine {
public:
    audition::BackendInfo backendInfo() const override { return {"installed-asr","1"}; }
    audition::AsrCapabilities capabilities() const override { return {}; }
    audition::Transcript transcribe(const audition::SpeechSegment& segment) const override {
        audition::Transcript value{};
        value.segment_id=segment.segment_id;
        value.text=std::to_string(segment.track_id->value())+":"+
                   std::to_string(segment.audio.samples().size());
        return value;
    }
};
audition::AudioView chunk(const std::vector<float>& pcm) {
    return audition::AudioView{
        pcm.data(),pcm.size(),{16000U,1U,audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
}
std::vector<audition::SourceSpeechResult> poll(
    audition::PerSourceSpeechRuntime& runtime, std::size_t expected) {
    std::vector<audition::SourceSpeechResult> received{};
    for (int attempt=0;attempt<3000 && received.size()<expected;++attempt) {
        auto batch=runtime.takeResults();
        for (auto& item:batch) received.push_back(std::move(item));
        if (received.size()<expected)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return received;
}
} // namespace

bool verifyInstalledSpeech() {
    try {
        audition::BoundedSpeechPcmIngress ingress{};
        const std::vector<float> initial(4U,0.3F);
        if (ingress.push(11U,chunk(initial))!=audition::SpeechPcmAdmission::Accepted)
            return false;
        auto accepted=ingress.tryPop();
        if (!accepted || accepted->kind!=audition::SpeechPcmIngressEvent::Kind::Audio ||
            accepted->audio.samples().size()!=4U) return false;
        ingress.forget(11U);
        auto reset=ingress.tryPop();
        if (!reset || reset->kind!=audition::SpeechPcmIngressEvent::Kind::Forget)
            return false;
        // Retirement keeps the final PCM ahead of the control event.
        if (ingress.push(12U,chunk(initial))!=audition::SpeechPcmAdmission::Accepted ||
            !ingress.retire(12U)) return false;
        auto final_audio=ingress.tryPop();
        auto retire_event=ingress.tryPop();
        if (!final_audio || !retire_event ||
            final_audio->kind!=audition::SpeechPcmIngressEvent::Kind::Audio ||
            retire_event->kind!=audition::SpeechPcmIngressEvent::Kind::Retire ||
            retire_event->source_id!=12U) return false;
        ingress.close();
        if (ingress.waitPop().has_value()) return false;
        TestVad vad{};
        audition::PerSourceSpeechOptions options{};
        options.min_speech_samples=4U;
        options.pre_roll_samples=0U;
        options.post_roll_samples=0U;
        audition::PerSourceSpeechRuntime runtime{
            vad,std::make_shared<TestAsr>(),options};
        const std::vector<float> voice(4U,0.5F);
        const std::vector<float> silence(4U,0.0F);
        runtime.push(21U,chunk(voice));
        runtime.push(22U,chunk(voice));
        runtime.push(21U,chunk(silence));
        runtime.push(22U,chunk(silence));
        auto first=poll(runtime,2U);
        if (first.size()!=2U) return false;
        const auto is21=[](const audition::SourceSpeechResult& r) {
            return r.track_id==21U;
        };
        const auto is22=[](const audition::SourceSpeechResult& r) {
            return r.track_id==22U;
        };
        const auto a=std::find_if(first.begin(),first.end(),is21);
        const auto b=std::find_if(first.begin(),first.end(),is22);
        if (a==first.end() || b==first.end() ||
            a->transcript.text!="21:4" || b->transcript.text!="22:4")
            return false;
        const auto retired=runtime.retire(21U);
        if (!retired || *retired!=a->generation) return false;
        const auto done=runtime.drainReady();
        if (!done.results.empty() ||
            std::find(done.ready_retired_generations.begin(),
                      done.ready_retired_generations.end(),*retired)
                ==done.ready_retired_generations.end())
            return false;
        runtime.releaseRetired(*retired);
        runtime.push(21U,chunk(voice));
        runtime.push(21U,chunk(silence));
        auto reused=poll(runtime,1U);
        if (reused.size()!=1U || reused[0].track_id!=21U ||
            reused[0].transcript.text!="21:4" ||
            reused[0].generation==*retired)
            return false;
        if (runtime.statistics().completed_segments!=3U) return false;
        // Exercise the installed async adapter, not just the underlying
        // synchronous runtime and standalone PCM queue.
        audition::PerSourceSpeechRuntime async_runtime{
            vad,std::make_shared<TestAsr>(),options};
        {
            audition::AsyncSpeechIngressWorker worker{async_runtime};
            if (worker.push(31U,chunk(voice))!=audition::SpeechPcmAdmission::Accepted ||
                worker.push(31U,chunk(silence))!=audition::SpeechPcmAdmission::Accepted)
                return false;
            const auto retired_epoch=worker.retire(31U);
            if (!retired_epoch) return false;
            bool bound_seen=false;
            bool retired_seen=false;
            std::optional<std::uint64_t> bound_generation;
            for (int attempt=0;attempt<3000 && !retired_seen;++attempt) {
                for (const auto& ack:worker.drainAcknowledgements()) {
                    if (ack.source_id!=31U || ack.epoch!=*retired_epoch ||
                        ack.kind==audition::AsyncSpeechIngressWorker::Acknowledgement::Kind::Error)
                        return false;
                    if (ack.kind==audition::AsyncSpeechIngressWorker::Acknowledgement::Kind::Bound) {
                        bound_seen=true;
                        bound_generation=ack.generation;
                    }
                    if (ack.kind==audition::AsyncSpeechIngressWorker::Acknowledgement::Kind::Retired) {
                        retired_seen=true;
                        if (!ack.generation || ack.generation!=bound_generation)
                            return false;
                    }
                }
                if (!retired_seen)
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            if (!bound_seen || !retired_seen) return false;
            worker.closeInput();
        }
        auto async_results=poll(async_runtime,1U);
        if (async_results.size()!=1U ||
            async_results[0].track_id!=31U ||
            async_results[0].transcript.text!="31:4")
            return false;
        return true;
    } catch (...) {
        return false;
    }
}
