#include <audition/backends/clap.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "detail/roberta_tokenizer.hpp"

namespace {

audition::ClapOnnxTextOptions validTextOptions() {
    audition::ClapOnnxTextOptions options{};
    options.model = "clap_text.onnx";
    options.tokenizer = "tokenizer.json";
    return options;
}

audition::ClapOpenVocabularyOptions validOpenVocabularyOptions() {
    audition::ClapOpenVocabularyOptions options{};
    options.audio.model = "clap_audio.onnx";
    options.text = validTextOptions();
    return options;
}

std::vector<std::int64_t> parseIds(const std::string& text) {
    std::vector<std::int64_t> values;
    std::stringstream stream{text};
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            values.push_back(std::stoll(item));
        }
    }
    return values;
}

audition::AudioBuffer sineWave(
    std::size_t sample_count,
    double frequency_hz) {
    constexpr double pi =
        3.14159265358979323846;
    constexpr double sample_rate_hz = 48000.0;

    std::vector<float> samples(sample_count);
    for (std::size_t i = 0U;
         i < samples.size();
         ++i) {
        const double phase =
            2.0 * pi * frequency_hz *
            static_cast<double>(i) /
            sample_rate_hz;
        samples[i] =
            static_cast<float>(
                0.1 * std::sin(phase));
    }

    return {
        std::move(samples),
        {48000U, 1U,
         audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
}

}  // namespace

TEST(ClapTextConfig, AcceptsCpuConfiguration) {
    const auto options = validTextOptions();
    EXPECT_NO_THROW(
        audition::validateClapOnnxTextOptions(
            options));
}

TEST(ClapTextConfig, RejectsMissingArtifacts) {
    auto options = validTextOptions();
    options.model.clear();
    EXPECT_THROW(
        audition::validateClapOnnxTextOptions(
            options),
        audition::Error);

    options = validTextOptions();
    options.tokenizer.clear();
    EXPECT_THROW(
        audition::validateClapOnnxTextOptions(
            options),
        audition::Error);
}

TEST(ClapTextConfig, RejectsIncompatibleContract) {
    auto options = validTextOptions();
    options.sequence_length = 76U;
    EXPECT_THROW(
        audition::validateClapOnnxTextOptions(
            options),
        audition::Error);

    options = validTextOptions();
    options.embedding_dimension = 256U;
    EXPECT_THROW(
        audition::validateClapOnnxTextOptions(
            options),
        audition::Error);

    options = validTextOptions();
    options.intra_op_threads = 0;
    EXPECT_THROW(
        audition::validateClapOnnxTextOptions(
            options),
        audition::Error);

    options = validTextOptions();
    options.execution.device_class =
        audition::DeviceClass::Npu;
    EXPECT_THROW(
        audition::validateClapOnnxTextOptions(
            options),
        audition::Error);
}

TEST(ClapOpenVocabularyConfig,
     RejectsMismatchedEmbeddingSpace) {
    auto options = validOpenVocabularyOptions();
    options.text.model_id = "different-space";
    EXPECT_THROW(
        audition::validateClapOpenVocabularyOptions(
            options),
        audition::Error);

    options = validOpenVocabularyOptions();
    options.text.embedding_dimension = 256U;
    EXPECT_THROW(
        audition::validateClapOpenVocabularyOptions(
            options),
        audition::Error);
}

TEST(ClapOpenVocabularyConfig,
     RejectsInvalidTemperature) {
    auto options = validOpenVocabularyOptions();
    options.similarity_temperature = 0.0;
    EXPECT_THROW(
        audition::validateClapOpenVocabularyOptions(
            options),
        audition::Error);

    options = validOpenVocabularyOptions();
    options.similarity_temperature =
        std::numeric_limits<double>::infinity();
    EXPECT_THROW(
        audition::validateClapOpenVocabularyOptions(
            options),
        audition::Error);
}

TEST(ClapBackend,
     ImplementsOpenVocabularyClassifierContract) {
    static_assert(
        std::is_base_of_v<
            audition::IOpenVocabularyAudioClassifier,
            audition::ClapOpenVocabularyClassifier>);
}

TEST(ClapTokenizer,
     MatchesPinnedReferenceWhenAvailable) {
    const char* tokenizer_path =
        std::getenv(
            "LIBAUDITION_TEST_CLAP_TOKENIZER");
    const char* golden_path =
        std::getenv(
            "LIBAUDITION_TEST_CLAP_TOKENIZER_GOLDEN");

    if (tokenizer_path == nullptr ||
        *tokenizer_path == '\0' ||
        golden_path == nullptr ||
        *golden_path == '\0') {
        GTEST_SKIP()
            << "CI-only CLAP tokenizer parity fixtures are not configured";
    }

    audition::clap_detail::RobertaTokenizer tokenizer{
        tokenizer_path};

    EXPECT_GT(tokenizer.vocabSize(), 40000U);
    EXPECT_EQ(tokenizer.bosId(), 0);
    EXPECT_EQ(tokenizer.padId(), 1);
    EXPECT_EQ(tokenizer.eosId(), 2);
    EXPECT_EQ(tokenizer.unkId(), 3);

    std::ifstream stream{golden_path};
    ASSERT_TRUE(stream.good());

    std::string line;
    std::size_t cases = 0U;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const auto first_tab = line.find('\t');
        const auto second_tab =
            line.find('\t', first_tab + 1U);
        ASSERT_NE(first_tab, std::string::npos);
        ASSERT_NE(second_tab, std::string::npos);

        const auto text =
            line.substr(0U, first_tab);
        const auto expected_ids =
            parseIds(line.substr(
                first_tab + 1U,
                second_tab - first_tab - 1U));
        const auto expected_mask =
            parseIds(line.substr(second_tab + 1U));

        const auto encoded =
            tokenizer.encode(text, 77U);

        EXPECT_EQ(
            encoded.input_ids,
            expected_ids)
            << "text: " << text;
        EXPECT_EQ(
            encoded.attention_mask,
            expected_mask)
            << "text: " << text;
        ++cases;
    }

    EXPECT_GE(cases, 4U);
}

TEST(ClapOpenVocabulary,
     RealModelsClassifyWhenAvailable) {
    const char* audio_model =
        std::getenv(
            "LIBAUDITION_TEST_CLAP_AUDIO_MODEL");
    const char* text_model =
        std::getenv(
            "LIBAUDITION_TEST_CLAP_TEXT_MODEL");
    const char* tokenizer =
        std::getenv(
            "LIBAUDITION_TEST_CLAP_TOKENIZER");

    if (audio_model == nullptr ||
        *audio_model == '\0' ||
        text_model == nullptr ||
        *text_model == '\0' ||
        tokenizer == nullptr ||
        *tokenizer == '\0') {
        GTEST_SKIP()
            << "CI-only CLAP open-vocabulary fixtures are not configured";
    }

    auto options =
        validOpenVocabularyOptions();
    options.audio.model = audio_model;
    options.text.model = text_model;
    options.text.tokenizer = tokenizer;

    audition::ClapOpenVocabularyClassifier classifier{
        options};

    const auto capabilities =
        classifier.capabilities();
    EXPECT_TRUE(capabilities.open_vocabulary);
    EXPECT_TRUE(capabilities.embeddings);
    ASSERT_EQ(
        capabilities.audio.supported_sample_rates_hz.size(),
        1U);
    EXPECT_EQ(
        capabilities.audio.supported_sample_rates_hz.front(),
        48000U);

    auto audio = sineWave(48000U, 440.0);
    const std::vector<std::string> candidates{
        "speech",
        "music",
        "dog barking"};

    const auto result =
        classifier.classify(
            audio.view(), candidates);

    ASSERT_EQ(
        result.classes.size(),
        candidates.size());

    double probability_sum = 0.0;
    std::set<std::string> observed;
    double previous = 1.0;

    for (const auto& score : result.classes) {
        const double probability =
            score.probability.value();
        EXPECT_GE(probability, 0.0);
        EXPECT_LE(probability, 1.0);
        EXPECT_LE(probability, previous + 1.0e-12);
        previous = probability;
        probability_sum += probability;
        observed.insert(score.label);
    }

    EXPECT_NEAR(probability_sum, 1.0, 1.0e-9);
    EXPECT_EQ(
        observed,
        std::set<std::string>(
            candidates.begin(),
            candidates.end()));

    const auto single =
        classifier.classify(
            audio.view(), {"music"});
    ASSERT_EQ(single.classes.size(), 1U);
    EXPECT_DOUBLE_EQ(
        single.classes.front().probability.value(),
        1.0);

    EXPECT_THROW(
        static_cast<void>(
            classifier.classify(
                audio.view(), {})),
        audition::Error);
    EXPECT_THROW(
        static_cast<void>(
            classifier.classify(
                audio.view(),
                {"music", "music"})),
        audition::Error);
    EXPECT_THROW(
        static_cast<void>(
            classifier.classify(
                audio.view(),
                {std::string{"caf\xC3\xA9"}})),
        audition::Error);
}
