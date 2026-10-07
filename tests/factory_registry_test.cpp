#include <audition/interfaces/classify.hpp>
#include <audition/registry/factory_registry.hpp>

#include <gtest/gtest.h>

namespace {
struct Config {};
class Classifier final : public audition::IAudioClassifier {
public:
    audition::BackendInfo backendInfo() const override { return {"test", "1"}; }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView) const override { return {}; }
};
}  // namespace

TEST(FactoryRegistry, CreatesRegisteredBackend) {
    audition::FactoryRegistry<audition::IAudioClassifier, Config> registry;
    registry.registerFactory("test", [](const Config&) { return std::make_unique<Classifier>(); });
    EXPECT_TRUE(registry.contains("test"));
    EXPECT_EQ(registry.create("test", {})->backendInfo().name, "test");
}

TEST(FactoryRegistry, RejectsUnknownBackend) {
    audition::FactoryRegistry<audition::IAudioClassifier, Config> registry;
    EXPECT_THROW(registry.create("missing", {}), audition::Error);
}
