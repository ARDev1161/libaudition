#include <audition/memory/in_memory_registries.hpp>

#include <gtest/gtest.h>
#include <audition/core/error.hpp>
#include <limits>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

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


TEST(MemoryRegistry, IdentifiersAreIndependentAcrossRegistryTypes) {
    audition::InMemoryAcousticSourceRegistry sources;
    audition::InMemorySpeakerRegistry speakers;
    audition::InMemorySoundPrototypeRegistry sounds;
    EXPECT_EQ(sources.create().value(), 1U);
    EXPECT_EQ(speakers.create("first").value(), 1U);
    EXPECT_EQ(sounds.create("first").value(), 1U);
    EXPECT_EQ(sources.create().value(), 2U);
    EXPECT_EQ(speakers.create("second").value(), 2U);
    EXPECT_EQ(sounds.create("second").value(), 2U);
}

TEST(MemoryRegistry, SourceSnapshotsCannotMutateRegistryContents) {
    audition::InMemoryAcousticSourceRegistry registry;
    const auto id = registry.create();
    const auto source = registry.find(id);
    ASSERT_TRUE(source.has_value());
    auto snapshot = *source;
    snapshot.fingerprints.push_back({});
    snapshot.associated_speaker = audition::SpeakerId{99U};
    auto listing = registry.list();
    ASSERT_EQ(listing.size(), 1U);
    listing.front().fingerprints.push_back({});
    EXPECT_TRUE(registry.find(id)->fingerprints.empty());
    EXPECT_FALSE(registry.find(id)->associated_speaker.has_value());
}

TEST(MemoryRegistry, SpeakerSnapshotsAreIndependentAndRenameIsPersistent) {
    audition::InMemorySpeakerRegistry registry;
    const auto id = registry.create("original");
    auto snapshot = registry.find(id);
    ASSERT_TRUE(snapshot.has_value());
    snapshot->display_name = "local-only";
    auto listing = registry.list();
    ASSERT_EQ(listing.size(), 1U);
    listing[0].display_name = "changed-again";
    EXPECT_EQ(registry.find(id)->display_name, "original");
    registry.rename(id, "renamed");
    EXPECT_EQ(registry.find(id)->display_name, "renamed");
    EXPECT_EQ(snapshot->display_name, "local-only");
}

TEST(MemoryRegistry, DuplicatePrototypeLabelsRemainSeparateEntries) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto first = registry.create("alarm");
    const auto second = registry.create("alarm");
    EXPECT_NE(first, second);
    audition::AudioEmbedding value{};
    value.model_id = "embed";
    value.values = {1.F, 0.F};
    registry.addExample(first, value);
    EXPECT_EQ(registry.find(first)->examples.size(), 1U);
    EXPECT_TRUE(registry.find(second)->examples.empty());
    EXPECT_EQ(registry.list().size(), 2U);
}

TEST(MemoryRegistry, PrototypeSnapshotsDoNotAliasExamples) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto id = registry.create("door");
    audition::AudioEmbedding value{};
    value.model_id = "embed";
    value.values = {1.F, 0.F};
    registry.addExample(id, value);
    auto snapshot = registry.find(id);
    ASSERT_TRUE(snapshot.has_value());
    snapshot->examples.front().values[0] = 500.F;
    snapshot->examples.push_back(value);
    auto listing = registry.list();
    ASSERT_EQ(listing.size(), 1U);
    listing.front().label = "mutated";
    ASSERT_EQ(registry.find(id)->examples.size(), 1U);
    EXPECT_FLOAT_EQ(registry.find(id)->examples.front().values[0], 1.F);
    EXPECT_EQ(registry.find(id)->label, "door");
}

TEST(MemoryRegistry, MalformedAppendPreservesPreviouslyStoredPrototypeExample) {
    audition::InMemorySoundPrototypeRegistry registry;
    const auto id = registry.create("door");
    audition::AudioEmbedding value{};
    value.model_id = "embed";
    value.values = {0.6F, 0.8F};
    registry.addExample(id, value);
    value.values = {0.F, 0.F};
    EXPECT_THROW(registry.addExample(id, value), audition::Error);
    const auto snapshot = registry.find(id);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->examples.size(), 1U);
    EXPECT_EQ(snapshot->examples.front().values,
              (std::vector<float>{0.6F, 0.8F}));
}

TEST(MemoryRegistry, SourceAssociationsCanBeReassignedWithoutChangingFingerprint) {
    audition::InMemoryAcousticSourceRegistry sources;
    const auto id = sources.create();
    sources.addFingerprint(id, {});
    sources.associateSpeaker(id, audition::SpeakerId{1U});
    sources.associateSpeaker(id, audition::SpeakerId{2U});
    const auto stored = sources.find(id);
    ASSERT_TRUE(stored.has_value());
    ASSERT_TRUE(stored->associated_speaker.has_value());
    EXPECT_EQ(stored->associated_speaker->value(), 2U);
    EXPECT_EQ(stored->fingerprints.size(), 1U);
}

TEST(MemoryRegistry, ConcurrentSourceCreationYieldsUniqueIds) {
    audition::InMemoryAcousticSourceRegistry registry;
    constexpr std::size_t workers = 4U;
    constexpr std::size_t per_worker = 32U;
    std::vector<std::vector<std::uint64_t>> ids(workers);
    std::vector<std::thread> threads;
    for (std::size_t index = 0U; index < workers; ++index) {
        threads.emplace_back([&, index] {
            for (std::size_t n = 0U; n < per_worker; ++n) {
                ids[index].push_back(registry.create().value());
            }
        });
    }
    for (auto& thread : threads) thread.join();
    std::vector<std::uint64_t> all;
    for (const auto& group : ids) {
        all.insert(all.end(), group.begin(), group.end());
    }
    std::sort(all.begin(), all.end());
    ASSERT_EQ(all.size(), workers * per_worker);
    for (std::size_t i = 0U; i < all.size(); ++i) {
        EXPECT_EQ(all[i], static_cast<std::uint64_t>(i + 1U));
    }
    EXPECT_EQ(registry.list().size(), workers * per_worker);
}
