#include <audition/model/in_memory_model_registry.hpp>
#include <audition/core/error.hpp>

#include <gtest/gtest.h>

TEST(ModelRegistry, StoresDescriptorById) {
    audition::InMemoryModelRegistry registry;
    audition::ModelDescriptor model;
    model.id = "asr.test";
    model.version = "1";
    model.license = {"Apache-2.0", true, true, false};
    registry.add(model);

    const auto found = registry.find("asr.test");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->license.spdx_id, "Apache-2.0");
}


TEST(ModelRegistry, RejectsEmptyIdentifierWithoutChangingRegistry) {
    audition::InMemoryModelRegistry registry;
    audition::ModelDescriptor invalid;
    EXPECT_THROW(registry.add(invalid), audition::Error);
    EXPECT_TRUE(registry.list().empty());
    EXPECT_FALSE(registry.find("").has_value());
    EXPECT_FALSE(registry.find("unknown").has_value());
}

TEST(ModelRegistry, ReplacesDescriptorsByIdentifierWithoutDuplicatingEntries) {
    audition::InMemoryModelRegistry registry;
    audition::ModelDescriptor first;
    first.id = "asr";
    first.version = "1";
    audition::ModelDescriptor second;
    second.id = "vad";
    second.version = "1";
    registry.add(first);
    registry.add(second);
    first.version = "2";
    registry.add(first);
    const auto found = registry.find("asr");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->version, "2");
    const auto entries = registry.list();
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_FALSE(registry.find("not-present").has_value());
}
