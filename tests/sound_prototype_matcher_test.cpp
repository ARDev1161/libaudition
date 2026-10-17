#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/sound_prototype_matcher.hpp>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace {

audition::AudioEmbedding embedding(
    std::string model_id,
    std::initializer_list<float> values) {
    audition::AudioEmbedding result{};
    result.model_id = std::move(model_id);
    result.values.assign(values.begin(), values.end());
    return result;
}

}  // namespace

TEST(AudioEmbeddingContract, UnknownQualityIsRepresentedAsMissing) {
    const audition::AudioEmbedding value{};
    EXPECT_FALSE(value.quality.has_value());
}

TEST(SoundPrototypeRegistry, RejectsMalformedEmbeddings) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto id = registry.create("alarm");

    auto value = embedding("", {1.0F, 0.0F});
    EXPECT_THROW(registry.addExample(id, value), audition::Error);

    value = embedding("clap", {});
    EXPECT_THROW(registry.addExample(id, value), audition::Error);

    value = embedding("clap", {0.0F, 0.0F});
    EXPECT_THROW(registry.addExample(id, value), audition::Error);

    value = embedding(
        "clap", {1.0F, std::numeric_limits<float>::quiet_NaN()});
    EXPECT_THROW(registry.addExample(id, value), audition::Error);
}

TEST(SoundPrototypeMatcher, RanksCentroidCosineSimilarity) {
    audition::InMemorySoundPrototypeRegistry registry;

    const auto grinder = registry.create("coffee_grinder");
    registry.addExample(grinder, embedding("clap-v1", {1.0F, 0.0F}));
    registry.addExample(grinder, embedding("clap-v1", {0.8F, 0.2F}));

    const auto bell = registry.create("door_bell");
    registry.addExample(bell, embedding("clap-v1", {0.0F, 1.0F}));

    const auto other_model = registry.create("other_model_example");
    registry.addExample(other_model, embedding("clap-v2", {1.0F, 0.0F}));

    audition::CosineSoundPrototypeMatcher matcher{registry};
    const auto result = matcher.match(
        embedding("clap-v1", {1.0F, 0.0F}));

    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0].prototype_id.value(), grinder.value());
    EXPECT_EQ(result[0].label, "coffee_grinder");
    EXPECT_GT(result[0].similarity.value, 0.98);
    EXPECT_EQ(result[1].prototype_id.value(), bell.value());
    EXPECT_NEAR(result[1].similarity.value, 0.0, 1.0e-9);
}

TEST(SoundPrototypeMatcher, AppliesThresholdAndResultLimit) {
    audition::InMemorySoundPrototypeRegistry registry;

    const auto first = registry.create("first");
    registry.addExample(first, embedding("clap", {1.0F, 0.0F}));

    const auto second = registry.create("second");
    registry.addExample(second, embedding("clap", {0.8F, 0.6F}));

    const auto third = registry.create("third");
    registry.addExample(third, embedding("clap", {0.0F, 1.0F}));

    audition::CosineSoundPrototypeMatcher matcher{registry};

    audition::SoundPrototypeMatchOptions options{};
    options.max_results = 1U;
    options.min_similarity = audition::Score{0.5};

    const auto result = matcher.match(
        embedding("clap", {1.0F, 0.0F}), options);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result.front().prototype_id.value(), first.value());
}

TEST(SoundPrototypeMatcher, RejectsInvalidQueryAndThreshold) {
    audition::InMemorySoundPrototypeRegistry registry;
    audition::CosineSoundPrototypeMatcher matcher{registry};

    EXPECT_THROW(
        static_cast<void>(
            matcher.match(embedding("clap", {0.0F, 0.0F}))),
        audition::Error);

    audition::SoundPrototypeMatchOptions options{};
    options.min_similarity = audition::Score{1.1};
    EXPECT_THROW(
        static_cast<void>(
            matcher.match(embedding("clap", {1.0F, 0.0F}), options)),
        audition::Error);
}


TEST(SoundPrototypeMatcher, EmptyRegistryAndUntrainedPrototypeProduceNoMatches) {
    audition::InMemorySoundPrototypeRegistry registry;
    audition::CosineSoundPrototypeMatcher matcher{registry};
    EXPECT_TRUE(matcher.match(embedding("model", {1.F, 0.F})).empty());
    static_cast<void>(registry.create("untrained"));
    EXPECT_TRUE(matcher.match(embedding("model", {1.F, 0.F})).empty());
}

TEST(SoundPrototypeMatcher, RejectsMalformedQueryEmbeddings) {
    audition::InMemorySoundPrototypeRegistry registry;
    audition::CosineSoundPrototypeMatcher matcher{registry};
    EXPECT_THROW(static_cast<void>(matcher.match(embedding("", {1.F, 0.F}))),
                 audition::Error);
    EXPECT_THROW(static_cast<void>(matcher.match(embedding("model", {}))),
                 audition::Error);
    EXPECT_THROW(static_cast<void>(matcher.match(embedding("model", {0.F, 0.F}))),
                 audition::Error);
    EXPECT_THROW(static_cast<void>(matcher.match(embedding("model",
        {std::numeric_limits<float>::infinity(), 1.F}))), audition::Error);
    EXPECT_THROW(static_cast<void>(matcher.match(embedding("model",
        {1.F, std::numeric_limits<float>::quiet_NaN()}))), audition::Error);
}

TEST(SoundPrototypeMatcher, RejectsNonfiniteAndOutOfRangeThresholds) {
    audition::InMemorySoundPrototypeRegistry registry;
    audition::CosineSoundPrototypeMatcher matcher{registry};
    audition::SoundPrototypeMatchOptions options{};
    for (const double invalid : {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -1.01, 1.01}) {
        options.min_similarity = audition::Score{invalid};
        EXPECT_THROW(static_cast<void>(matcher.match(
            embedding("model", {1.F, 0.F}), options)), audition::Error);
    }
}

TEST(SoundPrototypeMatcher, OrdersEqualScoresByPrototypeIdAndHonorsZeroLimit) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto first = registry.create("alpha");
    const auto second = registry.create("beta");
    const auto third = registry.create("gamma");
    registry.addExample(first, embedding("model", {1.F, 0.F}));
    registry.addExample(second, embedding("model", {1.F, 0.F}));
    registry.addExample(third, embedding("model", {1.F, 0.F}));
    audition::CosineSoundPrototypeMatcher matcher{registry};

    const auto ordered = matcher.match(embedding("model", {1.F, 0.F}));
    ASSERT_EQ(ordered.size(), 3U);
    EXPECT_EQ(ordered[0].prototype_id, first);
    EXPECT_EQ(ordered[1].prototype_id, second);
    EXPECT_EQ(ordered[2].prototype_id, third);

    audition::SoundPrototypeMatchOptions options{};
    options.max_results = 0U;
    EXPECT_TRUE(matcher.match(embedding("model", {1.F, 0.F}), options).empty());
    options.max_results = 2U;
    const auto limited = matcher.match(embedding("model", {1.F, 0.F}), options);
    ASSERT_EQ(limited.size(), 2U);
    EXPECT_EQ(limited[1].prototype_id, second);
}

TEST(SoundPrototypeMatcher, NormalizesEachTrainingExampleBeforeCentroid) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto id = registry.create("balanced");
    registry.addExample(id, embedding("model", {100.F, 0.F}));
    registry.addExample(id, embedding("model", {0.F, 1.F}));
    audition::CosineSoundPrototypeMatcher matcher{registry};
    const auto matches = matcher.match(embedding("model", {1.F, 0.F}));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].prototype_id, id);
    EXPECT_NEAR(matches[0].similarity.value, std::sqrt(0.5), 1e-9);
}

TEST(SoundPrototypeMatcher, AntipodalExamplesCancelAndAreSkipped) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto cancelled = registry.create("ambiguous");
    registry.addExample(cancelled, embedding("model", {1.F, 0.F}));
    registry.addExample(cancelled, embedding("model", {-1.F, 0.F}));
    const auto valid = registry.create("reference");
    registry.addExample(valid, embedding("model", {1.F, 0.F}));
    audition::CosineSoundPrototypeMatcher matcher{registry};
    const auto matches = matcher.match(embedding("model", {1.F, 0.F}));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].prototype_id, valid);
}

TEST(SoundPrototypeMatcher, FiltersOtherEmbeddingSpacesAndDimensions) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto id = registry.create("mixed");
    registry.addExample(id, embedding("other-model", {1.F, 0.F}));
    registry.addExample(id, embedding("model", {1.F, 0.F, 0.F}));
    registry.addExample(id, embedding("model", {0.F, 1.F}));
    audition::CosineSoundPrototypeMatcher matcher{registry};
    const auto matches = matcher.match(embedding("model", {0.F, 1.F}));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].prototype_id, id);
    EXPECT_NEAR(matches[0].similarity.value, 1.0, 1e-9);
    EXPECT_TRUE(matcher.match(embedding("model", {1.F, 0.F, 0.F, 0.F})).empty());
}

TEST(SoundPrototypeMatcher, NegativeCosineIsAllowedWhenThresholdPermits) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto opposite = registry.create("opposite");
    registry.addExample(opposite, embedding("model", {-1.F, 0.F}));
    audition::CosineSoundPrototypeMatcher matcher{registry};
    audition::SoundPrototypeMatchOptions options{};
    options.min_similarity = audition::Score{-1.0};
    const auto accepted = matcher.match(embedding("model", {1.F, 0.F}), options);
    ASSERT_EQ(accepted.size(), 1U);
    EXPECT_EQ(accepted[0].prototype_id, opposite);
    EXPECT_NEAR(accepted[0].similarity.value, -1.0, 1e-9);
    options.min_similarity = audition::Score{0.0};
    EXPECT_TRUE(matcher.match(embedding("model", {1.F, 0.F}), options).empty());
}

TEST(SoundPrototypeMatcher, ThresholdIncludesExactZeroSimilarityBoundary) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto orthogonal = registry.create("orthogonal");
    registry.addExample(orthogonal, embedding("model", {0.F, 1.F}));
    audition::CosineSoundPrototypeMatcher matcher{registry};
    audition::SoundPrototypeMatchOptions options{};
    options.min_similarity = audition::Score{0.0};
    const auto boundary = matcher.match(embedding("model", {1.F, 0.F}), options);
    ASSERT_EQ(boundary.size(), 1U);
    EXPECT_EQ(boundary[0].prototype_id, orthogonal);
    EXPECT_DOUBLE_EQ(boundary[0].similarity.value, 0.0);
    options.min_similarity = audition::Score{0.001};
    EXPECT_TRUE(matcher.match(embedding("model", {1.F, 0.F}), options).empty());
}
