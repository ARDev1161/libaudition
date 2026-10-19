#include <audition/speech/bounded_speech_pcm_ingress.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <limits>
#include <thread>
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
    EXPECT_EQ(ingress.push(2U,block(pcm)),SpeechPcmAdmission::Accepted);
    auto reset=ingress.tryPop();
    ASSERT_TRUE(reset.has_value());
    EXPECT_EQ(reset->kind,SpeechPcmIngressEvent::Kind::Forget);
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
