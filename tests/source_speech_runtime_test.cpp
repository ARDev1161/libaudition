#include <audition/speech/source_speech_runtime.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
class ThresholdVadSession final : public audition::IVadSession {
public:
    void reset() override {}
    audition::VadResult process(audition::AudioView audio) override {
        for (std::size_t i = 0U; i < audio.sampleCount(); ++i) {
            if (audio.data()[i] < -0.9F) throw std::runtime_error{"Bad VAD track"};
        }
        bool active = false;
        for (std::size_t i = 0U; i < audio.sampleCount(); ++i) {
            active |= std::fabs(audio.data()[i]) > 0.25F;
        }
        audition::VadResult result{};
        result.speech_active = active;
        return result;
    }
};

class ThresholdVad final : public audition::IVoiceActivityDetector {
public:
    audition::BackendInfo backendInfo() const override { return {"test-vad", "1"}; }
    audition::AudioRequirements audioRequirements() const override {
        audition::AudioRequirements req{};
        req.supported_sample_rates_hz = {16000U};
        req.preferred_frame_count = 160U;
        return req;
    }
    std::unique_ptr<audition::IVadSession> createSession() const override {
        return std::make_unique<ThresholdVadSession>();
    }
};

class FakeAsr final : public audition::IAsrEngine {
public:
    std::atomic<int> calls{0};
    std::atomic<bool> wait_first{false};
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};

    audition::BackendInfo backendInfo() const override { return {"test-asr", "1"}; }
    audition::AsrCapabilities capabilities() const override {
        audition::AsrCapabilities cap{};
        cap.audio.supported_sample_rates_hz = {16000U};
        return cap;
    }
    audition::Transcript transcribe(const audition::SpeechSegment& segment) const override {
        auto* self = const_cast<FakeAsr*>(this);
        const auto call = self->calls.fetch_add(1);
        if (call == 0 && self->wait_first.load()) {
            self->started.store(true);
            for (int i = 0; i < 2500 && !self->release.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }
        audition::Transcript result{};
        result.segment_id = segment.segment_id;
        result.text = "speaker-" + std::to_string(segment.track_id->value());
        return result;
    }
};

audition::SourceSpeechOptions options() {
    audition::SourceSpeechOptions opts{};
    opts.vad_frame_samples = 160U; // 10ms VAD frames
    opts.pre_roll_ms = 10U;
    opts.post_roll_ms = 20U;
    opts.min_speech_ms = 10U;
    opts.max_segment_ms = 1000U;
    opts.max_queued_chunks = 128U;
    return opts;
}

bool feed(audition::SourceSpeechRuntime& runtime, std::uint64_t id,
          float value, int frames) {
    std::vector<float> pcm(160U, value);
    for (int i = 0; i < frames; ++i) {
        audition::AudioView view{pcm.data(), pcm.size(),
            {16000U, 1U, audition::AudioLayout::Interleaved},
            audition::Timestamp{}, static_cast<std::uint64_t>(i)};
        if (!runtime.push(id, view)) return false;
    }
    return true;
}

template <typename Ready>
bool eventually(Ready ready) {
    for (int i = 0; i < 2000; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}
} // namespace

TEST(SourceSpeechRuntime, SeparatesSimultaneousSourcesAndPreservesTrackIdentity) {
    auto vad = std::make_shared<ThresholdVad>();
    auto asr = std::make_shared<FakeAsr>();
    audition::SourceSpeechRuntime runtime{vad, asr, options()};
    ASSERT_TRUE(feed(runtime, 7U, 0.F, 2));
    ASSERT_TRUE(feed(runtime, 8U, 0.F, 2));
    ASSERT_TRUE(feed(runtime, 7U, 0.8F, 3));
    ASSERT_TRUE(feed(runtime, 8U, 0.7F, 3));
    ASSERT_TRUE(feed(runtime, 7U, 0.F, 4));
    ASSERT_TRUE(feed(runtime, 8U, 0.F, 4));
    ASSERT_TRUE(eventually([&] {
        return runtime.status(7U).completed_segments == 1U &&
               runtime.status(8U).completed_segments == 1U;
    }));
    const auto output = runtime.drainTranscripts();
    ASSERT_EQ(output.size(), 2U);
    for (const auto& event : output) {
        EXPECT_EQ(event.transcript.text, "speaker-" +
                  std::to_string(event.source_id));
        EXPECT_EQ(event.transcript.segment_id.valid(), true);
        EXPECT_GT(event.audio_end_sample, event.audio_start_sample);
        EXPECT_EQ(event.audio_start_sample, 160U); // 10ms pre-roll
        EXPECT_EQ(event.audio_end_sample, 1120U); // 3 speech + 2 silence frames, see below
    }
    EXPECT_TRUE(runtime.drainTranscripts().empty());
}

TEST(SourceSpeechRuntime, ForgetInvalidatesInFlightRecognitionAndReusedId) {
    auto vad = std::make_shared<ThresholdVad>();
    auto asr = std::make_shared<FakeAsr>();
    asr->wait_first.store(true);
    audition::SourceSpeechRuntime runtime{vad, asr, options()};
    ASSERT_TRUE(feed(runtime, 17U, 0.8F, 3));
    ASSERT_TRUE(feed(runtime, 17U, 0.F, 4));
    const bool began = eventually([&] { return asr->started.load(); });
    runtime.forget(17U);
    asr->release.store(true);
    ASSERT_TRUE(began);
    ASSERT_TRUE(feed(runtime, 17U, 0.8F, 3));
    ASSERT_TRUE(feed(runtime, 17U, 0.F, 4));
    ASSERT_TRUE(eventually([&] {
        return runtime.status(17U).completed_segments == 1U;
    }));
    const auto output = runtime.drainTranscripts();
    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output.front().source_id, 17U);
    EXPECT_EQ(output.front().transcript.text, "speaker-17");
    EXPECT_EQ(asr->calls.load(), 2);
}

TEST(SourceSpeechRuntime, VadErrorIsolatedAcrossSources) {
    auto vad = std::make_shared<ThresholdVad>();
    auto asr = std::make_shared<FakeAsr>();
    audition::SourceSpeechRuntime runtime{vad, asr, options()};
    ASSERT_TRUE(feed(runtime, 4U, -1.F, 1));
    ASSERT_TRUE(eventually([&] {
        return !runtime.status(4U).error.empty();
    }));
    EXPECT_EQ(runtime.status(4U).error, "Bad VAD track");
    ASSERT_TRUE(feed(runtime, 5U, 0.8F, 3));
    ASSERT_TRUE(feed(runtime, 5U, 0.F, 3));
    ASSERT_TRUE(eventually([&] {
        return runtime.status(5U).completed_segments == 1U;
    }));
    EXPECT_TRUE(runtime.status(5U).error.empty());
    EXPECT_FALSE(runtime.push(4U, audition::AudioView{
        nullptr, 0U, {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}}) == false);
}

TEST(SourceSpeechRuntime, RejectsBadFormatAndSourceCapacity) {
    auto vad = std::make_shared<ThresholdVad>();
    auto asr = std::make_shared<FakeAsr>();
    auto opts = options();
    opts.max_sources = 1U;
    audition::SourceSpeechRuntime runtime{vad, asr, opts};
    std::vector<float> mono(160U, 0.1F);
    EXPECT_THROW(runtime.push(1U, audition::AudioView{
        mono.data(), mono.size(), {48000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}}), std::invalid_argument);
    ASSERT_TRUE(feed(runtime, 1U, 0.F, 1));
    EXPECT_FALSE(feed(runtime, 2U, 0.8F, 1));
    runtime.forget(1U);
    EXPECT_TRUE(feed(runtime, 2U, 0.8F, 1));
}
