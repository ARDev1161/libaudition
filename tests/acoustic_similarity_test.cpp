#include <audition/dsp/acoustic_similarity.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

namespace {

constexpr std::size_t kSamples = 4096;
const audition::AudioFormat kMono{16000U, 1U, audition::AudioLayout::Interleaved};

std::vector<float> noise(std::uint32_t seed, std::size_t count = kSamples) {
    std::vector<float> result(count);
    for (auto& sample : result) {
        seed = seed * 1664525U + 1013904223U;
        sample = (static_cast<float>((seed >> 8U) & 65535U) / 32768.F - 1.F) * 0.2F;
    }
    return result;
}

audition::AudioBuffer mono(std::vector<float> samples,
                           std::uint32_t rate = 16000U) {
    return audition::AudioBuffer{
        std::move(samples), {rate, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
}

}  // namespace

TEST(AcousticSimilarity, DelayedGainScaledCopyIsAlignedAndSpectrallySimilar) {
    auto original = noise(123U);
    std::vector<float> delayed(kSamples, 0.F);
    for (std::size_t i = 37U; i < delayed.size(); ++i) {
        delayed[i] = 0.4F * original[i - 37U];
    }

    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    auto a = mono(std::move(original));
    auto b = mono(std::move(delayed));
    const auto result = analyzer.compare(a.view(), b.view());

    ASSERT_TRUE(result.waveform_correlation.has_value());
    EXPECT_EQ(result.delay_samples, 37);
    EXPECT_GT(*result.waveform_correlation, 0.99999);
    ASSERT_TRUE(result.spectral_change_similarity.has_value());
    EXPECT_GT(*result.spectral_change_similarity, 0.96);
    EXPECT_GT(result.spectral_frames, 2U);
    EXPECT_EQ(result.compared_samples, kSamples - 37U);
}

TEST(AcousticSimilarity, AdvancedCopyUsesNegativeDelayAndCorrectOverlap) {
    auto original = noise(932U);
    std::vector<float> advanced(kSamples, 0.F);
    for (std::size_t i = 0; i + 43U < kSamples; ++i) {
        advanced[i] = 0.6F * original[i + 43U];
    }

    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    auto a = mono(std::move(original));
    auto b = mono(std::move(advanced));
    const auto result = analyzer.compare(a.view(), b.view());

    ASSERT_TRUE(result.waveform_correlation);
    EXPECT_EQ(result.delay_samples, -43);
    EXPECT_GT(*result.waveform_correlation, 0.99999);
    EXPECT_EQ(result.compared_samples, kSamples - 43U);
    ASSERT_TRUE(result.spectral_change_similarity);
    EXPECT_GT(*result.spectral_change_similarity, 0.96);
}

TEST(AcousticSimilarity, PolarityInversionRetainsSignedCorrelation) {
    auto original = noise(481U);
    auto inverted = original;
    for (auto& sample : inverted) {
        sample = -sample;
    }
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    auto a = mono(std::move(original));
    auto b = mono(std::move(inverted));
    const auto result = analyzer.compare(a.view(), b.view());
    ASSERT_TRUE(result.waveform_correlation.has_value());
    EXPECT_EQ(result.delay_samples, 0);
    EXPECT_LT(*result.waveform_correlation, -0.99999);
    ASSERT_TRUE(result.spectral_change_similarity.has_value());
    EXPECT_GT(*result.spectral_change_similarity, 0.999);
}

TEST(AcousticSimilarity, UnrelatedSignalsDoNotProduceWaveformDuplicateEvidence) {
    auto a = mono(noise(11U));
    auto b = mono(noise(284U));
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    const auto result = analyzer.compare(a.view(), b.view());
    ASSERT_TRUE(result.waveform_correlation.has_value());
    EXPECT_LT(std::abs(*result.waveform_correlation), 0.20);
    ASSERT_TRUE(result.spectral_change_similarity.has_value());
    EXPECT_LT(std::abs(*result.spectral_change_similarity), 0.30);
}

TEST(AcousticSimilarity, SharedDominantLeakageIsNotSpeakerIdentity) {
    // Two independently varying weak sources are mixed with the same dominant
    // leaked source. A high score must not be interpreted as speaker identity.
    const auto dominant = noise(891U);
    const auto independent_a = noise(128U);
    const auto independent_b = noise(129U);
    std::vector<float> left(kSamples);
    std::vector<float> right(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        left[i] = dominant[i] + 0.05F * independent_a[i];
        right[i] = dominant[i] + 0.05F * independent_b[i];
    }
    auto a = mono(std::move(left));
    auto b = mono(std::move(right));
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    const auto result = analyzer.compare(a.view(), b.view());
    ASSERT_TRUE(result.waveform_correlation);
    EXPECT_EQ(result.delay_samples, 0);
    EXPECT_GT(*result.waveform_correlation, 0.99);
    // No same-speaker decision is exposed by this API.
}

TEST(AcousticSimilarity, SilenceIsAbsenceOfEvidenceNotZeroSimilarity) {
    auto a = mono(noise(16U));
    auto silence = mono(std::vector<float>(kSamples, 0.F));
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    const auto result = analyzer.compare(a.view(), silence.view());
    EXPECT_FALSE(result.waveform_correlation);
    EXPECT_FALSE(result.spectral_change_similarity);
    EXPECT_EQ(result.compared_samples, 0U);
}

TEST(AcousticSimilarity, ConstantSignalCannotEstablishACorrelation) {
    auto a = mono(std::vector<float>(kSamples, 0.20F));
    auto b = mono(std::vector<float>(kSamples, 0.35F));
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    const auto result = analyzer.compare(a.view(), b.view());
    EXPECT_FALSE(result.waveform_correlation);
    EXPECT_FALSE(result.spectral_change_similarity);
}

TEST(AcousticSimilarity, TooShortWindowHasNoEvidence) {
    auto a = mono(noise(24U, 300));
    auto b = mono(noise(24U, 300));
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    const auto result = analyzer.compare(a.view(), b.view());
    EXPECT_FALSE(result.waveform_correlation);
    EXPECT_FALSE(result.spectral_change_similarity);
}

TEST(AcousticSimilarity, RejectsMismatchedRatesLengthsAndChannels) {
    auto a = mono(noise(20U));
    auto wrong_rate = mono(noise(20U), 48000U);
    auto wrong_length = mono(noise(20U, kSamples - 1));
    audition::AudioBuffer stereo{
        std::vector<float>(kSamples * 2, 0.1F),
        {16000U, 2U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    EXPECT_THROW(static_cast<void>(analyzer.compare(a.view(), wrong_rate.view())), audition::Error);
    EXPECT_THROW(static_cast<void>(analyzer.compare(a.view(), wrong_length.view())), audition::Error);
    EXPECT_THROW(static_cast<void>(analyzer.compare(a.view(), stereo.view())), audition::Error);
}

TEST(AcousticSimilarity, RejectsNonFinitePcm) {
    auto a = mono(noise(40U));
    auto b_values = noise(40U);
    b_values[100] = std::numeric_limits<float>::quiet_NaN();
    audition::AudioView b{b_values.data(), b_values.size(), kMono, audition::Timestamp{}};
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    EXPECT_THROW(static_cast<void>(analyzer.compare(a.view(), b)), audition::Error);
    b_values[100] = std::numeric_limits<float>::infinity();
    EXPECT_THROW(static_cast<void>(analyzer.compare(a.view(), b)), audition::Error);
}

TEST(AcousticSimilarity, AnalysisLengthIsBoundedByOptions) {
    auto a_values = noise(15U);
    auto b_values = a_values;
    for (std::size_t i = 2048U; i < b_values.size(); ++i) {
        b_values[i] = -a_values[i];
    }
    auto a = mono(std::move(a_values));
    auto b = mono(std::move(b_values));
    audition::dsp::AcousticSimilarityAnalyzer analyzer{{2048U, 0U, 1e-5}};
    const auto result = analyzer.compare(a.view(), b.view());
    ASSERT_TRUE(result.waveform_correlation);
    EXPECT_NEAR(*result.waveform_correlation, 1.0, 1e-6);
    EXPECT_EQ(result.compared_samples, 2048U);
}

TEST(AcousticSimilarity, RejectsUnboundedAndNonFiniteOptions) {
    using audition::dsp::AcousticSimilarityAnalyzer;
    EXPECT_THROW(AcousticSimilarityAnalyzer({128U, 0U, 1e-5}), audition::Error);
    EXPECT_THROW(AcousticSimilarityAnalyzer({65537U, 0U, 1e-5}), audition::Error);
    EXPECT_THROW(AcousticSimilarityAnalyzer({1024U, 300U, 1e-5}), audition::Error);
    EXPECT_THROW(AcousticSimilarityAnalyzer({8192U, 3000U, 1e-5}), audition::Error);
    EXPECT_THROW(AcousticSimilarityAnalyzer(
        {8192U, 128U, std::numeric_limits<double>::quiet_NaN()}), audition::Error);
    EXPECT_THROW(AcousticSimilarityAnalyzer({8192U, 128U, -0.1}), audition::Error);
}

TEST(AcousticSimilarity, CompetingSpectrallySimilarIndependentSourcesRemainUnclassified) {
    // Same gain envelope, independent carriers. Both scores are diagnostics;
    // the analyzer must not silently merge sources on spectral resemblance.
    auto x = noise(234U);
    auto y = noise(777U);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float envelope = (i / 256U) % 3U == 0U ? 0.15F : 1.F;
        x[i] *= envelope;
        y[i] *= envelope;
    }
    auto a = mono(std::move(x));
    auto b = mono(std::move(y));
    audition::dsp::AcousticSimilarityAnalyzer analyzer;
    const auto result = analyzer.compare(a.view(), b.view());
    ASSERT_TRUE(result.waveform_correlation);
    EXPECT_LT(std::abs(*result.waveform_correlation), 0.2);
    EXPECT_TRUE(result.spectral_change_similarity.has_value());
}
