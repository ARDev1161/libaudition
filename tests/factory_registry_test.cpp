#include <acoustic/interfaces/classify.hpp>
#include <acoustic/registry/factory_registry.hpp>

#include <gtest/gtest.h>

namespace {
struct Config {};
class Classifier final : public acoustic::IAudioClassifier {
public:
    acoustic::BackendInfo backendInfo() const override { return {"test", "1"}; }
    acoustic::ClassifierCapabilities capabilities() const override { return {}; }
    acoustic::ClassificationResult classify(acoustic::AudioView) const override { return {}; }
};
}  // namespace

TEST(FactoryRegistry, CreatesRegisteredBackend) {
    acoustic::FactoryRegistry<acoustic::IAudioClassifier, Config> registry;
    registry.registerFactory("test", [](const Config&) { return std::make_unique<Classifier>(); });
    EXPECT_TRUE(registry.contains("test"));
    EXPECT_EQ(registry.create("test", {})->backendInfo().name, "test");
}

TEST(FactoryRegistry, RejectsUnknownBackend) {
    acoustic::FactoryRegistry<acoustic::IAudioClassifier, Config> registry;
    EXPECT_THROW(registry.create("missing", {}), acoustic::Error);
}
