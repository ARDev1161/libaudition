#include <gtest/gtest.h>

#include <audition/classify/vocabulary.hpp>
#include <audition/classify/efficientat_frontend_spec.hpp>
#include <audition/classify/source_classification_runtime.hpp>
#include <audition/classify/progressive_source_classification_runtime.hpp>

#include <chrono>
#include <atomic>
#include <memory>
#include <limits>
#include <stdexcept>
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

namespace {
class WindowEdgeClassifier final : public audition::IAudioClassifier {
public:
    audition::BackendInfo backendInfo() const override { return {"window-edge-test", "1"}; }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView audio) const override {
        audition::ClassificationResult result;
        const bool first = audio.sampleCount() == 8U &&
            audio.data()[0] == 0.0F && audio.data()[7] == 0.7F;
        const bool overlapping = audio.sampleCount() == 8U &&
            audio.data()[0] == 0.2F && audio.data()[7] == 0.9F;
        result.classes.push_back({
            first ? "first" : (overlapping ? "overlapping" : "unexpected"),
            audition::Probability::from(0.7)});
        return result;
    }
};
}

TEST(SourceClassificationRuntime, SlidingWindowRetainsActualSamples) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<WindowEdgeClassifier>(), 16000U, 8U, 2U, 2U};
    const float first[8] = {0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F};
    runtime.push(8U, first, 8U);
    bool saw_first = false;
    for (int i = 0; i < 1000; ++i) {
        const auto status = runtime.status(8U);
        if (status.result.has_value()) {
            ASSERT_FALSE(status.result->classes.empty());
            EXPECT_EQ(status.result->classes[0].label, "first");
            saw_first = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(saw_first);
    const float second[2] = {0.8F, 0.9F};
    runtime.push(8U, second, 2U);
    bool saw_overlap = false;
    for (int i = 0; i < 1000; ++i) {
        const auto status = runtime.status(8U);
        if (status.result.has_value() && !status.result->classes.empty() &&
            status.result->classes[0].label == "overlapping") {
            saw_overlap = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(saw_overlap);
}

namespace {
class FailOnNegativeClassifier final : public audition::IAudioClassifier {
public:
    audition::BackendInfo backendInfo() const override { return {"per-track-errors", "1"}; }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView audio) const override {
        if (audio.data()[0] < 0.0F) throw std::runtime_error{"Invalid separated source"};
        audition::ClassificationResult result;
        result.classes.push_back({"Speech", audition::Probability::from(0.8)});
        return result;
    }
};
}

TEST(SourceClassificationRuntime, MalformedTrackDoesNotPoisonOtherTracks) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<ConstantClassifier>(), 16000U, 4U};
    const float invalid[2] = {0.2F, std::numeric_limits<float>::quiet_NaN()};
    runtime.push(11U, invalid, 2U);
    const auto broken = runtime.status(11U);
    EXPECT_EQ(broken.state, audition::SourceClassificationState::Error);
    EXPECT_FALSE(broken.error.empty());
    // Source 11 must be quarantined, but source 12 must remain functional.
    const float good[4] = {0.2F, 0.2F, 0.2F, 0.2F};
    runtime.push(12U, good, 4U);
    bool healthy = false;
    for (int i = 0; i < 500; ++i) {
        const auto state = runtime.status(12U);
        if (state.result.has_value()) {
            healthy = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(healthy);
    runtime.forget(11U);
    runtime.push(11U, good, 4U);
    bool recovered = false;
    for (int i = 0; i < 500; ++i) {
        if (runtime.status(11U).result.has_value()) {
            recovered = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(recovered);
}

TEST(SourceClassificationRuntime, ModelExceptionDoesNotStopOtherODASSources) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<FailOnNegativeClassifier>(), 16000U, 4U};
    const float bad[4] = {-0.1F, 0.2F, 0.3F, 0.4F};
    const float good[4] = {0.1F, 0.2F, 0.3F, 0.4F};
    runtime.push(22U, bad, 4U);
    bool failed = false;
    for (int i = 0; i < 500; ++i) {
        const auto state = runtime.status(22U);
        if (state.state == audition::SourceClassificationState::Error) {
            EXPECT_EQ(state.error, "Invalid separated source");
            failed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(failed);
    runtime.push(23U, good, 4U);
    bool classified = false;
    for (int i = 0; i < 500; ++i) {
        const auto state = runtime.status(23U);
        if (state.result.has_value()) {
            ASSERT_EQ(state.state, audition::SourceClassificationState::Classified);
            classified = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(classified);
    // A forgotten source identity may be reused after the failed generation.
    runtime.forget(22U);
    runtime.push(22U, good, 4U);
    bool reused = false;
    for (int i = 0; i < 500; ++i) {
        if (runtime.status(22U).result.has_value()) {
            reused = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(reused);
}

namespace {
class WindowNamedClassifier final : public audition::IAudioClassifier {
public:
    WindowNamedClassifier(std::string name, std::size_t frames, bool fail = false)
        : name_(std::move(name)), frames_(frames), fail_(fail) {}
    audition::BackendInfo backendInfo() const override { return {name_, "1"}; }
    audition::ClassifierCapabilities capabilities() const override {
        audition::ClassifierCapabilities caps{};
        caps.audio.supported_sample_rates_hz = {16000U};
        caps.audio.preferred_frame_count = frames_;
        return caps;
    }
    audition::ClassificationResult classify(audition::AudioView audio) const override {
        if (audio.sampleCount() != frames_) {
            throw std::runtime_error{"Unexpected model window size"};
        }
        if (fail_) throw std::runtime_error{"Refiner unavailable"};
        audition::ClassificationResult out{};
        out.classes.push_back({name_, audition::Probability::from(0.9)});
        return out;
    }
private:
    std::string name_{};
    std::size_t frames_{};
    bool fail_{false};
};
}

TEST(ProgressiveSourceClassificationRuntime, FastFirstThenFiveSecondRefinement) {
    std::vector<audition::ProgressiveClassificationStage> stages;
    stages.push_back({"fast-custom", std::make_unique<WindowNamedClassifier>("quick", 4U),
                      4U, 2U});
    stages.push_back({"long-custom", std::make_unique<WindowNamedClassifier>("precise", 12U),
                      12U, 4U});
    audition::ProgressiveSourceClassificationRuntime runtime{16000U, std::move(stages)};
    const float short_audio[4] = {0.1F,0.2F,0.3F,0.4F};
    runtime.push(15U, short_audio, 4U);
    bool fast = false;
    for (int i = 0; i < 1000; ++i) {
        const auto snapshot = runtime.status(15U);
        if (snapshot.selected_stage == 0U) {
            ASSERT_TRUE(snapshot.result.has_value());
            ASSERT_EQ(snapshot.stages.size(), 2U);
            EXPECT_EQ(snapshot.result->classes.front().label, "quick");
            EXPECT_FALSE(snapshot.stages[1].status.result.has_value());
            fast = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(fast);
    const float more[8] = {0.5F,0.6F,0.7F,0.8F,0.1F,0.2F,0.3F,0.4F};
    runtime.push(15U, more, 8U);
    bool refined = false;
    for (int i = 0; i < 1000; ++i) {
        const auto snapshot = runtime.status(15U);
        if (snapshot.selected_stage == 1U) {
            ASSERT_TRUE(snapshot.result.has_value());
            EXPECT_EQ(snapshot.result->classes.front().label, "precise");
            EXPECT_EQ(snapshot.stages[1].name, "long-custom");
            EXPECT_TRUE(snapshot.stages[0].status.result.has_value());
            refined = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(refined);
    runtime.forget(15U);
    const auto cleared = runtime.status(15U);
    EXPECT_FALSE(cleared.selected_stage.has_value());
    EXPECT_FALSE(cleared.result.has_value());
    ASSERT_EQ(cleared.stages.size(), 2U);
    EXPECT_EQ(cleared.stages[0].status.state,
              audition::SourceClassificationState::WaitingForAudio);
}

TEST(ProgressiveSourceClassificationRuntime, RefinerFailureKeepsFastResults) {
    std::vector<audition::ProgressiveClassificationStage> stages;
    stages.push_back({"fast", std::make_unique<WindowNamedClassifier>("quick", 4U),
                      4U, 2U});
    stages.push_back({"refined", std::make_unique<WindowNamedClassifier>("bad", 12U, true),
                      12U, 4U});
    audition::ProgressiveSourceClassificationRuntime runtime{16000U, std::move(stages)};
    const float audio[12] = {0.1F,0.1F,0.1F,0.1F,0.1F,0.1F,
                             0.1F,0.1F,0.1F,0.1F,0.1F,0.1F};
    runtime.push(21U, audio, 12U);
    bool isolated = false;
    for (int i = 0; i < 1000; ++i) {
        const auto state = runtime.status(21U);
        if (state.stages[1].status.state == audition::SourceClassificationState::Error &&
            state.selected_stage == 0U) {
            ASSERT_TRUE(state.result.has_value());
            EXPECT_EQ(state.result->classes.front().label, "quick");
            EXPECT_EQ(state.stages[1].status.error, "Refiner unavailable");
            isolated = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(isolated);
}

TEST(ProgressiveSourceClassificationRuntime, SupportsCustomWindowSizesAndRejectsInvalidStages) {
    auto make_stage = [](std::string name, std::size_t window, std::size_t hop) {
        return audition::ProgressiveClassificationStage{
            std::move(name), std::make_unique<WindowNamedClassifier>("custom", window),
            window, hop};
    };
    std::vector<audition::ProgressiveClassificationStage> one;
    one.push_back(make_stage("any-window", 7U, 3U));
    EXPECT_NO_THROW((audition::ProgressiveSourceClassificationRuntime{
        16000U, std::move(one), 2U}));
    std::vector<audition::ProgressiveClassificationStage> duplicate;
    duplicate.push_back(make_stage("duplicate", 4U, 2U));
    duplicate.push_back(make_stage("duplicate", 10U, 5U));
    EXPECT_THROW((audition::ProgressiveSourceClassificationRuntime{
        16000U, std::move(duplicate)}), std::invalid_argument);
    std::vector<audition::ProgressiveClassificationStage> too_long_hop;
    too_long_hop.push_back(make_stage("bad-hop", 8U, 9U));
    EXPECT_THROW((audition::ProgressiveSourceClassificationRuntime{
        16000U, std::move(too_long_hop)}), std::invalid_argument);
    std::vector<audition::ProgressiveClassificationStage> wrong_rate;
    wrong_rate.push_back(make_stage("wrong-rate", 4U, 2U));
    EXPECT_THROW((audition::ProgressiveSourceClassificationRuntime{
        32000U, std::move(wrong_rate)}), std::invalid_argument);
}

TEST(SourceClassificationRuntime, ReportsCompletedWindowAudioEndNotResultWallClock) {
    audition::SourceClassificationRuntime runtime{
        std::make_unique<WindowNamedClassifier>("window", 8U), 16000U, 8U, 2U, 2U};
    const float first[8] = {0.1F,0.1F,0.1F,0.1F,0.1F,0.1F,0.1F,0.1F};
    runtime.push(80U, first, 8U);
    bool first_ready = false;
    for (int i = 0; i < 1000; ++i) {
        const auto state = runtime.status(80U);
        if (state.result_end_sample == 8U) {
            EXPECT_EQ(state.received_samples, 8U);
            first_ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(first_ready);
    const float next[2] = {0.2F, 0.3F};
    runtime.push(80U, next, 2U);
    bool updated = false;
    for (int i = 0; i < 1000; ++i) {
        const auto state = runtime.status(80U);
        if (state.result_end_sample == 10U) {
            EXPECT_EQ(state.received_samples, 10U);
            EXPECT_EQ(state.collected_samples, 6U);
            updated = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(updated);
    runtime.forget(80U);
    const auto retired = runtime.status(80U);
    EXPECT_EQ(retired.received_samples, 0U);
    EXPECT_FALSE(retired.result_end_sample.has_value());
}

TEST(ProgressiveSourceClassificationRuntime, StaleLongWindowFallsBackToFreshQuickPrediction) {
    std::vector<audition::ProgressiveClassificationStage> stages;
    stages.push_back({"quick", std::make_unique<WindowNamedClassifier>("recent", 4U),
                      4U, 2U});
    // A long prediction may lag up to four PCM samples before losing priority.
    stages.push_back({"refined", std::make_unique<WindowNamedClassifier>("past", 8U),
                      8U, 8U, 4U});
    audition::ProgressiveSourceClassificationRuntime runtime{
        16000U, std::move(stages)};
    const float initial[8] = {0.1F,0.1F,0.1F,0.1F,0.1F,0.1F,0.1F,0.1F};
    runtime.push(22U, initial, 8U);
    bool refined = false;
    for (int i = 0; i < 1000; ++i) {
        const auto state = runtime.status(22U);
        if (state.selected_stage == 1U) {
            ASSERT_TRUE(state.result.has_value());
            EXPECT_EQ(state.result->classes.front().label, "past");
            EXPECT_EQ(state.stages[1].status.result_end_sample, 8U);
            refined = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(refined);
    // Five more samples are insufficient to fill the next long window.
    // The completed long result now belongs to an older acoustic event.
    const float after[5] = {0.2F,0.2F,0.2F,0.2F,0.2F};
    runtime.push(22U, after, 5U);
    bool quick_selected = false;
    for (int i = 0; i < 1000; ++i) {
        const auto state = runtime.status(22U);
        if (state.selected_stage == 0U &&
            state.stages[0].status.result_end_sample.has_value() &&
            *state.stages[0].status.result_end_sample >= 12U) {
            ASSERT_TRUE(state.result.has_value());
            EXPECT_EQ(state.result->classes.front().label, "recent");
            EXPECT_EQ(state.stages[1].status.received_samples, 13U);
            EXPECT_EQ(state.stages[1].status.result_end_sample, 8U);
            EXPECT_TRUE(state.stages[1].status.result.has_value());
            quick_selected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(quick_selected);
}

namespace {
class GatedWindowClassifier final : public audition::IAudioClassifier {
public:
    GatedWindowClassifier(std::atomic<bool>& started, std::atomic<bool>& release)
        : started_(started), release_(release) {}
    audition::BackendInfo backendInfo() const override { return {"gated", "1"}; }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView audio) const override {
        if (!started_.exchange(true)) {
            while (!release_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }
        audition::ClassificationResult result{};
        result.classes.push_back({
            std::to_string(audio.data()[audio.sampleCount()-1U]),
            audition::Probability::one()});
        return result;
    }
private:
    std::atomic<bool>& started_;
    std::atomic<bool>& release_;
};
}

TEST(SourceClassificationRuntime, OverloadedSourceCoalescesToNewestQueuedWindow) {
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    audition::SourceClassificationRuntime runtime{
        std::make_unique<GatedWindowClassifier>(started, release),
        16000U, 4U, 2U, 2U};
    const float first[4] = {0.1F,0.2F,0.3F,0.4F};
    runtime.push(6U, first, 4U);
    for (int i = 0; i < 1000 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const bool started_inference = started.load();
    const float later[10] = {0.5F,0.6F,0.7F,0.8F,0.9F,
                             1.0F,0.1F,0.2F,0.3F,0.4F};
    runtime.push(6U, later, 10U);
    const auto during = runtime.status(6U);
    EXPECT_EQ(during.received_samples, 14U);
    EXPECT_EQ(during.dropped_windows, 4U);
    release.store(true);
    ASSERT_TRUE(started_inference);
    bool newest = false;
    for (int i = 0; i < 1000; ++i) {
        const auto status = runtime.status(6U);
        if (status.result_end_sample == 14U) {
            ASSERT_TRUE(status.result.has_value());
            EXPECT_EQ(status.result->classes.front().label, std::to_string(0.4F));
            newest = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(newest);
}

TEST(SourceClassificationRuntime, ReportsPerSourceDropCountsAndTiming) {
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    audition::SourceClassificationRuntime runtime{
        std::make_unique<GatedWindowClassifier>(started, release),
        16000U, 4U, 2U, 2U};
    const float first[4] = {0.1F, 0.2F, 0.3F, 0.4F};
    runtime.push(10U, first, 4U);
    for (int i = 0; i < 1000 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_TRUE(started.load());
    const float backlog[8] = {0.1F, 0.2F, 0.3F, 0.4F,
                              0.5F, 0.6F, 0.7F, 0.8F};
    runtime.push(10U, backlog, 8U);
    runtime.push(11U, first, 4U);
    const auto overloaded = runtime.status(10U);
    const auto healthy = runtime.status(11U);
    EXPECT_GT(overloaded.dropped_windows, 0U);
    EXPECT_EQ(healthy.dropped_windows, 0U);
    EXPECT_EQ(overloaded.completed_windows, 0U);
    release.store(true);
    bool both_completed = false;
    for (int i = 0; i < 1000; ++i) {
        const auto first_state = runtime.status(10U);
        const auto second_state = runtime.status(11U);
        if (first_state.completed_windows >= 2U &&
            second_state.completed_windows >= 1U) {
            EXPECT_TRUE(first_state.last_queue_wait_ms.has_value());
            EXPECT_TRUE(first_state.last_inference_ms.has_value());
            EXPECT_GE(*first_state.last_queue_wait_ms, 0.0);
            EXPECT_GE(*first_state.last_inference_ms, 0.0);
            EXPECT_TRUE(second_state.last_queue_wait_ms.has_value());
            EXPECT_TRUE(second_state.last_inference_ms.has_value());
            both_completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(both_completed);
}
