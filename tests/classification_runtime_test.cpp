#include <gtest/gtest.h>

#include <audition/classify/vocabulary.hpp>
#include <audition/classify/source_classification_runtime.hpp>

#include <chrono>
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
