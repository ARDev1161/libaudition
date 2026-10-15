#include <audition/memory/in_memory_registries.hpp>

#include <gtest/gtest.h>
#include <audition/core/error.hpp>
#include <limits>

TEST(MemoryRegistry, KeepsGenericSourceSeparateFromSpeaker) {
    audition::InMemoryAcousticSourceRegistry sources;
    audition::InMemorySpeakerRegistry speakers;

    const auto source = sources.create();
    const auto speaker = speakers.create("Ivan");
    sources.associateSpeaker(source, speaker);

    const auto stored = sources.find(source);
    ASSERT_TRUE(stored.has_value());
    ASSERT_TRUE(stored->associated_speaker.has_value());
    EXPECT_EQ(stored->associated_speaker->value(), speaker.value());
}

TEST(MemoryRegistry, StoresFewShotSoundExamples) {
    audition::InMemorySoundPrototypeRegistry sounds;
    const auto id = sounds.create("coffee_grinder");
    audition::AudioEmbedding embedding;
    embedding.model_id = "clap";
    embedding.values = {0.1F, 0.2F};
    sounds.addExample(id, embedding);
    ASSERT_TRUE(sounds.find(id).has_value());
    EXPECT_EQ(sounds.find(id)->examples.size(), 1U);
}


TEST(MemoryRegistry, RejectsUnknownIdsAndListsCreatedProfiles) {
    audition::InMemoryAcousticSourceRegistry sources;
    audition::InMemorySpeakerRegistry speakers;
    audition::InMemorySoundPrototypeRegistry sounds;
    EXPECT_TRUE(sources.list().empty());
    EXPECT_TRUE(speakers.list().empty());
    EXPECT_TRUE(sounds.list().empty());
    EXPECT_FALSE(sources.find(audition::AcousticSourceId{999}).has_value());
    EXPECT_FALSE(speakers.find(audition::SpeakerId{999}).has_value());
    EXPECT_FALSE(sounds.find(audition::SoundPrototypeId{999}).has_value());
    EXPECT_THROW(sources.associateSpeaker(audition::AcousticSourceId{999},
                                         audition::SpeakerId{1}), audition::Error);
    EXPECT_THROW(sources.addFingerprint(audition::AcousticSourceId{999}, {}),
                 audition::Error);
    EXPECT_THROW(speakers.rename(audition::SpeakerId{999}, "absent"), audition::Error);
    EXPECT_THROW(speakers.addEmbedding(audition::SpeakerId{999}, {}), audition::Error);
    EXPECT_THROW(sounds.create(""), audition::Error);
    auto source = sources.create();
    auto speaker = speakers.create("original");
    auto sound = sounds.create("bell");
    EXPECT_EQ(sources.list().size(), 1U);
    EXPECT_EQ(speakers.list().size(), 1U);
    EXPECT_EQ(sounds.list().size(), 1U);
    sources.addFingerprint(source, {});
    speakers.addEmbedding(speaker, {});
    speakers.rename(speaker, "updated");
    EXPECT_EQ(sources.find(source)->fingerprints.size(), 1U);
    EXPECT_EQ(speakers.find(speaker)->embeddings.size(), 1U);
    EXPECT_EQ(speakers.find(speaker)->display_name, "updated");
    EXPECT_TRUE(sounds.find(sound).has_value());
}

TEST(MemoryRegistry, RejectsMalformedAudioEmbeddingsWithoutMutatingPrototype) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto id = registry.create("alarm");
    audition::AudioEmbedding embedding;
    embedding.model_id = "clap";
    embedding.values = {1.F};
    EXPECT_THROW(registry.addExample(audition::SoundPrototypeId{999}, embedding),
                 audition::Error);
    auto reject = [&](const audition::AudioEmbedding& invalid) {
        EXPECT_THROW(registry.addExample(id, invalid), audition::Error);
        EXPECT_TRUE(registry.find(id)->examples.empty());
    };
    embedding.model_id.clear();
    reject(embedding);
    embedding.model_id = "clap";
    embedding.values.clear();
    reject(embedding);
    embedding.values = {0.F, 0.F};
    reject(embedding);
    embedding.values = {std::numeric_limits<float>::quiet_NaN()};
    reject(embedding);
    embedding.values = {std::numeric_limits<float>::infinity()};
    reject(embedding);
    embedding.values = {0.3F, -0.4F};
    EXPECT_NO_THROW(registry.addExample(id, embedding));
    EXPECT_EQ(registry.find(id)->examples.size(), 1U);
}
