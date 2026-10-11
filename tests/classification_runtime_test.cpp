#include <gtest/gtest.h>

#include <audition/classify/vocabulary.hpp>
#include <audition/classify/efficientat_frontend_spec.hpp>
#include <audition/classify/source_classification_runtime.hpp>

#include <chrono>
#include <atomic>
#include <memory>
#include <thread>

namespace {
class ConstantClassifier final : public audition::IAudioClassifier {
public:
    audition::BackendInfo backendInfo() const override { return {"test", "1"}; }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView audio) const override {
        if (audio.sampleCount() == 0U) return {};
        audition::ClassificationResult result;
        result.classes.push_back({"Speech", audition::Probability::from(0.8)});
        result.classes.push_back({"Music", audition::Probability::from(0.2)});
        return result;
    }
};
}

TEST(Vocabulary, ExplicitUnsupportedClassDoesNotFabricateScore) {
    audition::ClassificationResult result{};
    result.classes.push_back({"Speech", audition::Probability::from(0.8)});
    const audition::VocabularySelection selected{
        audition::VocabularyMode::SelectedClasses, {"Speech", "Footsteps"}, 5U};
    const auto out = audition::selectVocabulary(
        result, selected, {"Speech", "Music"});
    ASSERT_EQ(out.classes.size(), 1U);
    EXPECT_EQ(out.classes.front().label, "Speech");
    ASSERT_EQ(out.unsupported_labels.size(), 1U);
    EXPECT_EQ(out.unsupported_labels.front(), "Footsteps");
}

TEST(Vocabulary, SelectedNotInTruncatedTopKIsNotUnsupported) {
    audition::ClassificationResult result{};
    result.classes.push_back({"Speech", audition::Probability::from(0.8)});
    const audition::VocabularySelection selected{
        audition::VocabularyMode::SelectedClasses, {"Music"}, 5U};
    const auto out = audition::selectVocabulary(
        result, selected, {"Speech", "Music"});
    EXPECT_TRUE(out.classes.empty());
    EXPECT_TRUE(out.unsupported_labels.empty());
}

TEST(SourceClassificationRuntime, ProcessesSeparatedAudioWithoutQt) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<ConstantClassifier>(), 16000U, 16U};
    const float samples[16] = {0.1F};
    runtime.push(7U, samples, 16U);
    bool classified = false;
    for (int i = 0; i < 200; ++i) {
        const auto state = runtime.status(7U);
        if (state.state == audition::SourceClassificationState::Classified) {
            ASSERT_TRUE(state.result.has_value());
            ASSERT_FALSE(state.result->classes.empty());
            EXPECT_EQ(state.result->classes.front().label, "Speech");
            classified = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(classified);
    runtime.forget(7U);
    EXPECT_EQ(runtime.status(7U).state,
              audition::SourceClassificationState::WaitingForAudio);
}

namespace {
class SlowClassifier final : public audition::IAudioClassifier {
public:
    explicit SlowClassifier(std::atomic<bool>& started, std::atomic<bool>& finish)
        : started_(started), finish_(finish) {}
    audition::BackendInfo backendInfo() const override { return {"slow", "1"}; }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView) const override {
        started_.store(true);
        while (!finish_.load()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
        audition::ClassificationResult result;
        result.classes.push_back({"OldSource", audition::Probability::one()});
        return result;
    }
private:
    std::atomic<bool>& started_;
    std::atomic<bool>& finish_;
};
}

TEST(SourceClassificationRuntime, ForgottenTrackCannotReceiveOldInference) {
    std::atomic<bool> started{false};
    std::atomic<bool> finish{false};
    {
        audition::SourceClassificationRuntime runtime{
            std::make_unique<SlowClassifier>(started, finish), 16000U, 4U};
        const float samples[4] = {0.1F, 0.2F, 0.3F, 0.4F};
        runtime.push(7U, samples, 4U);
        for (int i = 0; i < 1000 && !started.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        const bool began = started.load();
        runtime.forget(7U);
        runtime.push(7U, samples, 2U);
        finish.store(true);
        EXPECT_TRUE(began);
        for (int i = 0; i < 1000; ++i) {
            const auto state = runtime.status(7U);
            if (state.state != audition::SourceClassificationState::Inferencing) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        const auto state = runtime.status(7U);
        EXPECT_FALSE(state.result.has_value());
    }
}

TEST(SourceClassificationRuntime, ZeroValuedSourceIdIsAccepted) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<ConstantClassifier>(), 16000U, 4U};
    const float samples[4] = {0.1F, 0.2F, 0.3F, 0.4F};
    runtime.push(0U, samples, 4U);
    bool classified = false;
    for (int i = 0; i < 200; ++i) {
        if (runtime.status(0U).result.has_value()) {
            classified = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(classified);
}

TEST(SourceClassificationRuntime, BoundedQueueDoesNotDropOtherTrackWindow) {
    std::atomic<bool> started{false};
    std::atomic<bool> finish{false};
    audition::SourceClassificationRuntime runtime{
        std::make_unique<SlowClassifier>(started, finish), 16000U, 4U, 2U};
    const float samples[4] = {0.1F, 0.2F, 0.3F, 0.4F};
    runtime.push(1U, samples, 4U);
    for (int i = 0; i < 1000 && !started.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    runtime.push(2U, samples, 4U);
    const auto pending = runtime.status(2U);
    EXPECT_EQ(pending.state, audition::SourceClassificationState::Queued);
    finish.store(true);
}

TEST(EfficientAtFrontendSpec, RejectsSilentRateOrChannelMismatch) {
    const audition::EfficientAtFrontendSpec spec{};
    EXPECT_TRUE(spec.valid());
    EXPECT_EQ(spec.fft_size, 1024U);
    EXPECT_EQ(spec.mel_bins, 128U);
    EXPECT_NO_THROW(spec.requireMono32k(32000U, 1U));
    EXPECT_THROW(spec.requireMono32k(16000U, 1U), std::invalid_argument);
    EXPECT_THROW(spec.requireMono32k(32000U, 2U), std::invalid_argument);
}

TEST(SourceClassificationRuntime, EvictsLeastRecentlyUpdatedSource) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<ConstantClassifier>(), 16000U, 8U, 2U};
    const float sample = 0.1F;
    runtime.push(99U, &sample, 1U);
    runtime.push(10U, &sample, 1U);
    runtime.push(99U, &sample, 1U); // 99 is most recently updated
    runtime.push(25U, &sample, 1U); // must retire 10, not smallest ID
    EXPECT_EQ(runtime.status(10U).state,
              audition::SourceClassificationState::WaitingForAudio);
    EXPECT_EQ(runtime.status(99U).collected_samples, 2U);
    EXPECT_EQ(runtime.status(25U).collected_samples, 1U);
}

TEST(SourceClassificationRuntime, SlidingWindowRetainsOverlap) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<ConstantClassifier>(), 16000U, 8U, 2U, 2U};
    const float values[8] = {0.1F,0.2F,0.3F,0.4F,0.5F,0.6F,0.7F,0.8F};
    runtime.push(42U, values, 8U);
    EXPECT_EQ(runtime.status(42U).collected_samples, 6U);
    for (int i=0; i<200 && !runtime.status(42U).result.has_value(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    ASSERT_TRUE(runtime.status(42U).result.has_value());
    const float followup[2] = {0.9F,1.0F};
    runtime.push(42U, followup, 2U);
    EXPECT_EQ(runtime.status(42U).collected_samples, 6U);
    EXPECT_EQ(runtime.status(42U).required_samples, 8U);
}

TEST(SourceClassificationRuntime, RejectsHopLongerThanWindow) {
    EXPECT_THROW((audition::SourceClassificationRuntime{
        std::make_unique<ConstantClassifier>(), 16000U, 8U, 2U, 9U}), std::invalid_argument);
}
