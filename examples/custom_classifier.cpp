#include <audition/audition.hpp>

#include <memory>
#include <string>

namespace {

struct ClassifierConfig {
    std::string label{"custom_sound"};
};

class ConstantClassifier final : public audition::IAudioClassifier {
public:
    explicit ConstantClassifier(std::string label) : label_(std::move(label)) {}

    audition::BackendInfo backendInfo() const override { return {"example.constant", "1"}; }

    audition::ClassifierCapabilities capabilities() const override { return {}; }

    audition::ClassificationResult classify(audition::AudioView) const override {
        return {{{label_, audition::Probability::one()}}};
    }

private:
    std::string label_;
};

}  // namespace

int main() {
    audition::FactoryRegistry<audition::IAudioClassifier, ClassifierConfig> registry;
    registry.registerFactory("constant", [](const ClassifierConfig& config) {
        return std::make_unique<ConstantClassifier>(config.label);
    });

    auto classifier = registry.create("constant", ClassifierConfig{"coffee_grinder"});
    audition::AudioFormat format{16000, 1, audition::AudioLayout::Interleaved};
    audition::AudioBuffer audio{{0.0F, 0.0F}, format, audition::Timestamp::monotonicNow()};
    const auto result = classifier->classify(audio.view());
    return result.classes.size() == 1U ? 0 : 1;
}
