#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/source_identity_resolver.hpp>
#include <audition/pipeline/acoustic_event_assembler.hpp>
#include <audition/pipeline/spatial_identity.hpp>
#include <audition/dsp/audio_frontend.hpp>
#include <audition/classify/source_classification_runtime.hpp>
#include <audition/speech/per_source_speech_runtime.hpp>

#include <cstdint>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

namespace {

audition::Timestamp ts(std::int64_t nanoseconds) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, 0U}};
}

class NullFusion final : public audition::ISpatialFusion {
public:
    audition::BackendInfo backendInfo() const override {
        return {"scenario-null-fusion", "1"};
    }

    std::optional<audition::PositionEstimate> fuse(
        const audition::SpatialFusionInput&) const override {
        return std::nullopt;
    }
};

audition::SourceFingerprint fingerprint(
    float x,
    float y) {
    audition::SourceFingerprint result{};
    result.model_id = "scenario-acoustic-fingerprint-v1";
    result.embedding = {x, y};
    result.quality = audition::Probability::one();
    return result;
}

audition::SpatialTrack track(
    std::uint64_t track_id,
    std::int64_t first_seen,
    std::int64_t last_seen,
    audition::SourceFingerprint source_fingerprint) {
    audition::SpatialTrack result{};
    result.track_id = audition::SpatialTrackId{track_id};
    result.first_seen = ts(first_seen);
    result.last_seen = ts(last_seen);
    result.direction = {
        audition::Direction3D::fromVector({1.0, 0.0, 0.0}),
        0.02,
        audition::Probability::one()};
    result.fingerprint = std::move(source_fingerprint);
    return result;
}

}  // namespace

TEST(AcousticPipelineScenario, PersistentSourceSurvivesTrackerHandoffInsideEvent) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    NullFusion fusion;
    audition::SpatialIdentityCoordinator identity{
        fusion,
        resolver};
    audition::AcousticEventAssembler events;

    auto first_track = track(
        11U,
        0,
        10,
        fingerprint(1.0F, 0.0F));

    const auto first_identity =
        identity.observe(first_track);
    ASSERT_TRUE(first_identity.newly_created);
    ASSERT_TRUE(first_track.source_id.has_value());

    const auto event_id = events.begin(ts(0));
    events.updateFromTrack(event_id, first_track);

    audition::AcousticEventPatch classification_patch{};
    classification_patch.timestamp = ts(15);
    audition::ClassificationResult classification{};
    classification.classes.push_back(
        {"alarm", audition::Probability::from(0.95)});
    classification_patch.classification =
        classification;
    events.update(event_id, classification_patch);

    identity.endTrack(first_track.track_id, ts(20));

    auto replacement_track = track(
        37U,
        25,
        30,
        fingerprint(0.999F, 0.01F));

    const auto replacement_identity =
        identity.observe(replacement_track);
    ASSERT_FALSE(replacement_identity.newly_created);
    ASSERT_TRUE(replacement_track.source_id.has_value());
    EXPECT_EQ(
        *replacement_track.source_id,
        *first_track.source_id);

    // AcousticEventAssembler accepts this transient track switch because the
    // persistent source identity proves continuity.
    events.updateFromTrack(event_id, replacement_track);

    audition::AcousticEventPatch transcript_patch{};
    transcript_patch.timestamp = ts(35);
    audition::Transcript transcript{};
    transcript.segment_id = audition::SpeechSegmentId{8U};
    transcript.text = "help";
    transcript.language = "en";
    transcript.confidence =
        audition::Probability::from(0.9);
    transcript_patch.transcript = transcript;
    events.update(event_id, transcript_patch);

    const audition::AcousticEvent completed =
        events.finish(event_id, ts(40));

    EXPECT_EQ(completed.start_time.nanoseconds(), 0);
    EXPECT_EQ(completed.end_time.nanoseconds(), 40);

    ASSERT_TRUE(completed.track_id.has_value());
    EXPECT_EQ(
        *completed.track_id,
        replacement_track.track_id);

    ASSERT_TRUE(completed.source_id.has_value());
    EXPECT_EQ(
        *completed.source_id,
        *first_track.source_id);

    ASSERT_TRUE(completed.classification.has_value());
    ASSERT_EQ(
        completed.classification->classes.size(),
        1U);
    EXPECT_EQ(
        completed.classification->classes[0].label,
        "alarm");

    ASSERT_TRUE(completed.transcript.has_value());
    EXPECT_EQ(completed.transcript->text, "help");

    EXPECT_EQ(events.activeCount(), 0U);
    EXPECT_EQ(registry.list().size(), 1U);
}

TEST(AcousticPipelineScenario, SeparateActiveSourcesRemainSeparateEvents) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    NullFusion fusion;
    audition::SpatialIdentityCoordinator identity{
        fusion,
        resolver};
    audition::AcousticEventAssembler events;

    auto left = track(
        1U,
        0,
        10,
        fingerprint(1.0F, 0.0F));
    auto right = track(
        2U,
        0,
        11,
        fingerprint(0.0F, 1.0F));

    static_cast<void>(identity.observe(left));
    static_cast<void>(identity.observe(right));

    ASSERT_TRUE(left.source_id.has_value());
    ASSERT_TRUE(right.source_id.has_value());
    EXPECT_NE(*left.source_id, *right.source_id);

    const auto left_event = events.begin(ts(0));
    const auto right_event = events.begin(ts(0));
    events.updateFromTrack(left_event, left);
    events.updateFromTrack(right_event, right);

    const auto active = events.activeEvents();
    ASSERT_EQ(active.size(), 2U);
    ASSERT_TRUE(active[0].source_id.has_value());
    ASSERT_TRUE(active[1].source_id.has_value());
    EXPECT_NE(
        *active[0].source_id,
        *active[1].source_id);
}


namespace {

class SignClassifier final : public audition::IAudioClassifier {
public:
    audition::BackendInfo backendInfo() const override { return {"scenario-classifier", "1"}; }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView audio) const override {
        audition::ClassificationResult result{};
        result.classes.push_back({
            audio.data()[0] > 0.F ? "speech" : "machinery",
            audition::Probability::one()});
        return result;
    }
};

class SignVadSession final : public audition::IVadSession {
public:
    void reset() override {}
    audition::VadResult process(audition::AudioView audio) override {
        bool speech = false;
        for (std::size_t i = 0; i < audio.sampleCount(); ++i)
            if (audio.data()[i] > 0.25F) speech = true;
        return {std::nullopt, speech};
    }
};

class SignVad final : public audition::IVoiceActivityDetector {
public:
    audition::BackendInfo backendInfo() const override { return {"scenario-vad", "1"}; }
    audition::AudioRequirements audioRequirements() const override { return {}; }
    std::unique_ptr<audition::IVadSession> createSession() const override {
        return std::make_unique<SignVadSession>();
    }
};

class TrackTagAsr final : public audition::IAsrEngine {
public:
    audition::BackendInfo backendInfo() const override { return {"scenario-asr", "1"}; }
    audition::AsrCapabilities capabilities() const override { return {}; }
    audition::Transcript transcribe(const audition::SpeechSegment& segment) const override {
        audition::Transcript result{};
        result.segment_id = segment.segment_id;
        result.text = std::to_string(segment.track_id->value()) + ":" +
                      std::to_string(segment.audio.frameCount());
        return result;
    }
};

std::vector<audition::CompletedSourceClassification> waitForClassifications(
    audition::SourceClassificationRuntime& runtime, std::size_t count) {
    std::vector<audition::CompletedSourceClassification> completed;
    for (int attempt = 0; attempt < 1500 && completed.size() < count; ++attempt) {
        auto ready = runtime.takeCompleted();
        completed.insert(completed.end(),
                         std::make_move_iterator(ready.begin()),
                         std::make_move_iterator(ready.end()));
        if (completed.size() < count)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return completed;
}

std::vector<audition::SourceSpeechResult> waitForSpeech(
    audition::PerSourceSpeechRuntime& runtime, std::size_t count) {
    std::vector<audition::SourceSpeechResult> completed;
    for (int attempt = 0; attempt < 1500 && completed.size() < count; ++attempt) {
        auto ready = runtime.takeResults();
        completed.insert(completed.end(),
                         std::make_move_iterator(ready.begin()),
                         std::make_move_iterator(ready.end()));
        if (completed.size() < count)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return completed;
}

audition::AudioBuffer monoAudio(const std::vector<float>& samples,
                                std::int64_t timestamp_ns) {
    return audition::AudioBuffer{
        samples, {16000U, 1U, audition::AudioLayout::Interleaved},
        ts(timestamp_ns)};
}

} // namespace

TEST(AcousticPipelineScenario, RoutedPcmClassificationAndSpeechPreserveSourceBinding) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    NullFusion fusion;
    audition::SpatialIdentityCoordinator identity{fusion, resolver};
    audition::AcousticEventAssembler events;

    auto voice = track(11U, 0, 10, fingerprint(1.F, 0.F));
    auto machine = track(22U, 0, 11, fingerprint(0.F, 1.F));
    static_cast<void>(identity.observe(voice));
    static_cast<void>(identity.observe(machine));
    ASSERT_TRUE(voice.source_id.has_value());
    ASSERT_TRUE(machine.source_id.has_value());
    ASSERT_NE(*voice.source_id, *machine.source_id);
    const auto voice_event = events.begin(ts(0));
    const auto machine_event = events.begin(ts(0));
    events.updateFromTrack(voice_event, voice);
    events.updateFromTrack(machine_event, machine);

    // Real audio frontend and per-source schedulers, deterministic fake model
    // outputs. Applications route by persistent AcousticSourceId; the library
    // does not secretly orchestrate these independent components.
    audition::dsp::AudioFrontendConfig frontend_config{};
    frontend_config.routes = {{0U, {}}, {1U, {}}};
    audition::dsp::AudioFrontend frontend{frontend_config};
    std::vector<float> stereo_pcm;
    for (std::size_t i = 0; i < 8U; ++i) {
        stereo_pcm.push_back(0.75F);
        stereo_pcm.push_back(-0.5F);
    }
    const audition::AudioBuffer captured{
        stereo_pcm, {16000U, 2U, audition::AudioLayout::Interleaved}, ts(12)};
    const auto routed = frontend.process(captured.view());
    ASSERT_EQ(routed.frameCount(), 8U);
    ASSERT_EQ(routed.format().channel_count, 2U);
    std::vector<float> voice_pcm;
    std::vector<float> machine_pcm;
    for (std::size_t frame = 0; frame < routed.frameCount(); ++frame) {
        voice_pcm.push_back(routed.channel(0U)[frame]);
        machine_pcm.push_back(routed.channel(1U)[frame]);
    }

    audition::SourceClassificationRuntime classify{
        std::make_unique<SignClassifier>(), 16000U, 8U, 2U};
    classify.push(voice.source_id->value(), voice_pcm.data(), voice_pcm.size());
    classify.push(machine.source_id->value(), machine_pcm.data(), machine_pcm.size());
    const auto predictions = waitForClassifications(classify, 2U);
    ASSERT_EQ(predictions.size(), 2U);

    for (const auto& prediction : predictions) {
        ASSERT_EQ(prediction.start_sample, 0U);
        ASSERT_EQ(prediction.end_sample, 8U);
        ASSERT_EQ(prediction.result.classes.size(), 1U);
        audition::AcousticEventPatch patch{};
        patch.timestamp = ts(15);
        patch.classification = prediction.result;
        if (prediction.source_id == voice.source_id->value()) {
            EXPECT_EQ(prediction.result.classes.front().label, "speech");
            events.update(voice_event, patch);
        } else if (prediction.source_id == machine.source_id->value()) {
            EXPECT_EQ(prediction.result.classes.front().label, "machinery");
            events.update(machine_event, patch);
        } else {
            FAIL() << "Classification from unknown persistent source";
        }
    }
    EXPECT_EQ(classify.status(voice.source_id->value()).dropped_windows, 0U);
    EXPECT_EQ(classify.status(machine.source_id->value()).dropped_windows, 0U);

    SignVad vad;
    audition::PerSourceSpeechOptions options{};
    options.min_speech_samples = 8U;
    options.pre_roll_samples = 0U;
    options.post_roll_samples = 0U;
    audition::PerSourceSpeechRuntime speech{
        vad, std::make_shared<TrackTagAsr>(), options};
    const auto voice_audio = monoAudio(voice_pcm, 16);
    const auto machine_audio = monoAudio(machine_pcm, 16);
    speech.push(voice.track_id.value(), voice_audio.view());
    speech.push(machine.track_id.value(), machine_audio.view());
    const auto silence_audio = monoAudio(std::vector<float>(8U, 0.F), 17);
    speech.push(voice.track_id.value(), silence_audio.view());
    speech.push(machine.track_id.value(), silence_audio.view());
    const auto transcripts = waitForSpeech(speech, 1U);
    ASSERT_EQ(transcripts.size(), 1U);
    EXPECT_EQ(transcripts.front().track_id, voice.track_id.value());
    EXPECT_EQ(transcripts.front().transcript.text, "11:8");
    EXPECT_TRUE(speech.takeResults().empty());
    audition::AcousticEventPatch words{};
    words.timestamp = ts(20);
    words.transcript = transcripts.front().transcript;
    events.update(voice_event, words);

    // The physical source survives the upstream tracker changing its ID.
    identity.endTrack(voice.track_id, ts(22));
    auto successor = track(37U, 25, 30, fingerprint(0.999F, 0.01F));
    const auto match = identity.observe(successor);
    ASSERT_FALSE(match.newly_created);
    ASSERT_TRUE(successor.source_id.has_value());
    EXPECT_EQ(*successor.source_id, *voice.source_id);
    events.updateFromTrack(voice_event, successor);

    const auto spoken = events.finish(voice_event, ts(35));
    const auto noise = events.finish(machine_event, ts(35));
    ASSERT_TRUE(spoken.source_id.has_value());
    ASSERT_TRUE(noise.source_id.has_value());
    EXPECT_NE(*spoken.source_id, *noise.source_id);
    EXPECT_EQ(*spoken.track_id, successor.track_id);
    ASSERT_TRUE(spoken.classification.has_value());
    ASSERT_TRUE(noise.classification.has_value());
    EXPECT_EQ(spoken.classification->classes.front().label, "speech");
    EXPECT_EQ(noise.classification->classes.front().label, "machinery");
    ASSERT_TRUE(spoken.transcript.has_value());
    EXPECT_EQ(spoken.transcript->text, "11:8");
    EXPECT_FALSE(noise.transcript.has_value());
    EXPECT_EQ(events.activeCount(), 0U);
}

TEST(AcousticPipelineScenario, MalformedSourcePcmCannotPolluteOtherEventAndCanRecover) {
    audition::InMemoryAcousticSourceRegistry registry;
    audition::HeuristicSourceIdentityResolver resolver{registry};
    NullFusion fusion;
    audition::SpatialIdentityCoordinator identity{fusion, resolver};
    audition::AcousticEventAssembler events;
    auto broken = track(101U, 0, 10, fingerprint(1.F, 0.F));
    auto healthy = track(102U, 0, 10, fingerprint(0.F, 1.F));
    static_cast<void>(identity.observe(broken));
    static_cast<void>(identity.observe(healthy));
    ASSERT_TRUE(broken.source_id.has_value());
    ASSERT_TRUE(healthy.source_id.has_value());
    ASSERT_NE(*broken.source_id, *healthy.source_id);
    const auto broken_event = events.begin(ts(0));
    const auto healthy_event = events.begin(ts(0));
    events.updateFromTrack(broken_event, broken);
    events.updateFromTrack(healthy_event, healthy);

    audition::SourceClassificationRuntime classify{
        std::make_unique<SignClassifier>(), 16000U, 8U, 2U};
    const std::vector<float> invalid(8U, std::numeric_limits<float>::quiet_NaN());
    const std::vector<float> good(8U, -0.5F);
    classify.push(broken.source_id->value(), invalid.data(), invalid.size());
    EXPECT_EQ(classify.status(broken.source_id->value()).state,
              audition::SourceClassificationState::Error);
    classify.push(healthy.source_id->value(), good.data(), good.size());
    auto completed = waitForClassifications(classify, 1U);
    ASSERT_EQ(completed.size(), 1U);
    EXPECT_EQ(completed.front().source_id, healthy.source_id->value());
    EXPECT_EQ(completed.front().result.classes.front().label, "machinery");

    audition::AcousticEventPatch good_patch{};
    good_patch.timestamp = ts(15);
    good_patch.classification = completed.front().result;
    events.update(healthy_event, good_patch);

    // A late/corrupt source association must fail atomically and leave
    // previously recorded evidence for the healthy source intact.
    audition::AcousticEventPatch wrong_patch{};
    wrong_patch.timestamp = ts(16);
    wrong_patch.source_id = *broken.source_id;
    EXPECT_THROW(events.update(healthy_event, wrong_patch), audition::Error);
    const auto healthy_snapshot = events.find(healthy_event);
    ASSERT_TRUE(healthy_snapshot.has_value());
    ASSERT_TRUE(healthy_snapshot->source_id.has_value());
    EXPECT_EQ(*healthy_snapshot->source_id, *healthy.source_id);
    ASSERT_TRUE(healthy_snapshot->classification.has_value());
    EXPECT_EQ(healthy_snapshot->classification->classes.front().label, "machinery");
    EXPECT_EQ(healthy_snapshot->end_time.nanoseconds(), 15);

    classify.forget(broken.source_id->value());
    const std::vector<float> recovered_pcm(8U, 0.75F);
    classify.push(broken.source_id->value(),
                  recovered_pcm.data(), recovered_pcm.size());
    completed = waitForClassifications(classify, 1U);
    ASSERT_EQ(completed.size(), 1U);
    EXPECT_EQ(completed.front().source_id, broken.source_id->value());
    EXPECT_EQ(completed.front().start_sample, 0U);
    EXPECT_EQ(completed.front().end_sample, 8U);
    EXPECT_EQ(completed.front().result.classes.front().label, "speech");
    audition::AcousticEventPatch recovered{};
    recovered.timestamp = ts(20);
    recovered.classification = completed.front().result;
    events.update(broken_event, recovered);

    const auto first = events.finish(broken_event, ts(25));
    const auto second = events.finish(healthy_event, ts(25));
    ASSERT_TRUE(first.classification.has_value());
    ASSERT_TRUE(second.classification.has_value());
    EXPECT_EQ(first.classification->classes.front().label, "speech");
    EXPECT_EQ(second.classification->classes.front().label, "machinery");
    EXPECT_EQ(*first.source_id, *broken.source_id);
    EXPECT_EQ(*second.source_id, *healthy.source_id);
}
