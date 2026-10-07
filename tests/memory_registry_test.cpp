#include <audition/memory/in_memory_registries.hpp>

#include <gtest/gtest.h>

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
