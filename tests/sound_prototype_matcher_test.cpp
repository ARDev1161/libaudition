#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/sound_prototype_matcher.hpp>

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
