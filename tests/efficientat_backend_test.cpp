#include <audition/backends/efficientat/audio_tagger.hpp>
#include <audition/audio/audio_buffer.hpp>
#include <audition/backends/efficientat/resampler.hpp>
#include <audition/core/error.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {
std::filesystem::path fixtureDir() {
    const auto* env = std::getenv("LIBAUDITION_EFFICIENTAT_FIXTURE_DIR");
    return env ? std::filesystem::path{env} : std::filesystem::path{};
}
}

TEST(EfficientAtBackend, SyntheticLogitsAndOrdering) {
    const auto root = fixtureDir();
    if (root.empty()) GTEST_SKIP() << "Synthetic ONNX fixture not configured";
    audition::EfficientAtOnnxOptions options{};
    options.model = root / "efficientat_synthetic.onnx";
    options.labels = root / "labels.txt";
    options.top_k = 3U;
    const audition::EfficientAtSpectrogramTagger tagger{options};
    std::vector<float> mel(128U * 10U, 0.0F);
    const auto result = tagger.classifyLogMel(mel.data(), 128U, 10U);
    ASSERT_EQ(result.classes.size(), 3U);
    EXPECT_EQ(result.classes[0].label, "class_526");
    EXPECT_EQ(result.classes[1].label, "class_1");
    EXPECT_EQ(result.classes[2].label, "class_0");
    EXPECT_NEAR(result.classes[0].probability.value(), 1.0, 1.e-6);
    EXPECT_NEAR(result.classes[1].probability.value(), 1.0, 1.e-6);
    EXPECT_THROW(tagger.classifyLogMel(mel.data(), 127U, 10U), audition::Error);
    mel[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(tagger.classifyLogMel(mel.data(), 128U, 10U), audition::Error);
}

TEST(EfficientAtFrontend, SilenceHasExpectedLogFloor) {
    audition::EfficientAtWaveformFrontend frontend{};
    std::vector<float> samples(32000U, 0.0F);
    const audition::AudioBuffer buffer{
        std::move(samples), {32000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}, 0U};
    const auto result = frontend.compute(buffer.view());
    EXPECT_EQ(result.mel_bins, 128U);
    EXPECT_EQ(result.frames, 100U);
    ASSERT_EQ(result.values.size(), 128U * 100U);
    const double expected = (std::log(1.e-5) + 4.5) / 5.0;
    for (float v : result.values) EXPECT_NEAR(v, expected, 1.e-5);
}

TEST(EfficientAtBackend, EndToEndMonoWaveform) {
    const auto root = fixtureDir();
    if (root.empty()) GTEST_SKIP() << "Synthetic ONNX fixture not configured";
    audition::EfficientAtOnnxOptions options{};
    options.model = root / "efficientat_synthetic.onnx";
    options.labels = root / "labels.txt";
    const audition::EfficientAtAudioTagger tagger{options};
    std::vector<float> samples(32000U, 0.0F);
    const audition::AudioBuffer buffer{
        std::move(samples), {32000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}, 0U};
    const auto result = tagger.classify(buffer.view());
    ASSERT_FALSE(result.classes.empty());
    EXPECT_EQ(result.classes.front().label, "class_526");
    EXPECT_THROW(
        tagger.classify(audition::AudioView{
            buffer.samples().data(), buffer.samples().size(),
            {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}}),
        audition::Error);
}

TEST(EfficientAtResampler, ConstantSignalAndSampleCount) {
    std::vector<float> input(16000U, 0.25F);
    const auto output = audition::efficientAtUpsample16To32(input.data(), input.size());
    ASSERT_EQ(output.size(), 32000U);
    for (float value : output) EXPECT_NEAR(value, 0.25F, 1.e-5);
}

TEST(EfficientAtBackend, Source16kBridgeEndToEnd) {
    const auto root = fixtureDir();
    if (root.empty()) GTEST_SKIP() << "Synthetic ONNX fixture not configured";
    audition::EfficientAtOnnxOptions options{};
    options.model = root / "efficientat_synthetic.onnx";
    options.labels = root / "labels.txt";
    audition::EfficientAt16kAudioTagger tagger{options};
    std::vector<float> signal(16000U, 0.0F);
    const audition::AudioBuffer audio{
        std::move(signal), {16000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}, 0U};
    const auto result = tagger.classify(audio.view());
    ASSERT_FALSE(result.classes.empty());
    EXPECT_EQ(result.classes.front().label, "class_526");
}
