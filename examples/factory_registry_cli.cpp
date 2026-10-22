// A dependency-free example of explicitly linked backend selection.
// This is a mock classifier: it never loads weights or performs inference.
#include <audition/interfaces/classify.hpp>
#include <audition/registry/factory_registry.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
struct DemoConfig {
    std::string label;
};

class DemoClassifier final : public audition::IAudioClassifier {
public:
    explicit DemoClassifier(std::string label) : label_(std::move(label)) {}

    audition::BackendInfo backendInfo() const override {
        return {"mock-" + label_, "1"};
    }
    audition::ClassifierCapabilities capabilities() const override { return {}; }
    audition::ClassificationResult classify(audition::AudioView) const override {
        return {};  // Deliberately no inference; only factory wiring is demonstrated.
    }

private:
    std::string label_;
};
}  // namespace

int main(int argc, char** argv) {
    try {
        audition::FactoryRegistry<audition::IAudioClassifier, DemoConfig> registry;
        registry.registerFactory("linked-demo", [](const DemoConfig& config) {
            return std::make_unique<DemoClassifier>(config.label);
        });
        if (argc == 2 && std::string{argv[1]} == "--unknown") {
            try {
                const auto ignored = registry.create("not-linked", {"unused"});
                (void)ignored;
            } catch (const audition::Error& error) {
                if (error.code() == audition::ErrorCode::BackendUnavailable) {
                    std::cout << "factory-unknown=blocked\n";
                    return 0;
                }
                throw;
            }
            throw std::runtime_error("unknown backend was accepted");
        }
        if (argc == 2 && std::string{argv[1]} == "--duplicate") {
            try {
                registry.registerFactory("linked-demo", [](const DemoConfig& config) {
                    return std::make_unique<DemoClassifier>(config.label);
                });
            } catch (const audition::Error& error) {
                if (error.code() == audition::ErrorCode::ConfigurationError) {
                    std::cout << "factory-duplicate=blocked\n";
                    return 0;
                }
                throw;
            }
            throw std::runtime_error("duplicate factory was accepted");
        }
        if (argc != 1) throw std::runtime_error("usage: factory_registry_cli [--unknown|--duplicate]");
        const auto backend = registry.create("linked-demo", {"bell"});
        if (!registry.contains("linked-demo") || registry.names().size() != 1U ||
            backend->backendInfo().name != "mock-bell") {
            throw std::runtime_error("factory registration/configuration mismatch");
        }
        std::cout << "backend=" << backend->backendInfo().name << "\nfactory=ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "factory=error reason=" << error.what() << '\n';
        return 1;
    }
}
