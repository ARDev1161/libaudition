#include <audition/backends/efficientat/audio_tagger.hpp>
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
    EXPECT_EQ(result.classes[1].label, "class_0");
    EXPECT_NEAR(result.classes[0].probability.value(), 1.0 / (1.0 + std::exp(-4.0)), 1.e-6);
    EXPECT_NEAR(result.classes[1].probability.value(), 1.0 / (1.0 + std::exp(-2.0)), 1.e-6);
    EXPECT_THROW(tagger.classifyLogMel(mel.data(), 127U, 10U), audition::Error);
    mel[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(tagger.classifyLogMel(mel.data(), 128U, 10U), audition::Error);
}
