#include <audition/model/in_memory_model_registry.hpp>

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
