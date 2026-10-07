#include <acoustic/acoustic.hpp>

#include <memory>
#include <string>

namespace {

struct ClassifierConfig {
    std::string label{"custom_sound"};
};

class ConstantClassifier final : public acoustic::IAudioClassifier {
public:
    explicit ConstantClassifier(std::string label) : label_(std::move(label)) {}

    acoustic::BackendInfo backendInfo() const override { return {"example.constant", "1"}; }

    acoustic::ClassifierCapabilities capabilities() const override { return {}; }

    acoustic::ClassificationResult classify(acoustic::AudioView) const override {
        return {{{label_, acoustic::Probability::one()}}};
    }

private:
    std::string label_;
};

}  // namespace

int main() {
    acoustic::FactoryRegistry<acoustic::IAudioClassifier, ClassifierConfig> registry;
    registry.registerFactory("constant", [](const ClassifierConfig& config) {
        return std::make_unique<ConstantClassifier>(config.label);
    });

    auto classifier = registry.create("constant", ClassifierConfig{"coffee_grinder"});
    acoustic::AudioFormat format{16000, 1, acoustic::AudioLayout::Interleaved};
    acoustic::AudioBuffer audio{{0.0F, 0.0F}, format, acoustic::Timestamp::monotonicNow()};
    const auto result = classifier->classify(audio.view());
    return result.classes.size() == 1U ? 0 : 1;
}
