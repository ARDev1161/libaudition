#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <acoustic/core/error.hpp>

namespace acoustic {

/**
 * @brief Thread-safe registry for explicitly linked backend factories.
 *
 * This registry intentionally does not load shared objects. Applications can
 * register factories supplied by linked libraries or by their own code.
 */
template <typename Product, typename Config>
class FactoryRegistry {
public:
    using Factory = std::function<std::unique_ptr<Product>(const Config&)>;

    void registerFactory(std::string name, Factory factory) {
        if (name.empty() || !factory) {
            throw Error{ErrorCode::InvalidArgument, "Backend factory requires a name and callable"};
        }
        std::lock_guard<std::mutex> lock{mutex_};
        const auto [it, inserted] = factories_.emplace(std::move(name), std::move(factory));
        if (!inserted) {
            throw Error{ErrorCode::ConfigurationError, "A backend factory with this name is already registered"};
        }
    }

    [[nodiscard]] bool contains(const std::string& name) const {
        std::lock_guard<std::mutex> lock{mutex_};
        return factories_.find(name) != factories_.end();
    }

    [[nodiscard]] std::unique_ptr<Product> create(const std::string& name, const Config& config) const {
        Factory factory;
        {
            std::lock_guard<std::mutex> lock{mutex_};
            const auto it = factories_.find(name);
            if (it == factories_.end()) {
                throw Error{ErrorCode::BackendUnavailable, "Requested backend is not registered: " + name};
            }
            factory = it->second;
        }
        return factory(config);
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::lock_guard<std::mutex> lock{mutex_};
        std::vector<std::string> result;
        result.reserve(factories_.size());
        for (const auto& entry : factories_) {
            result.push_back(entry.first);
        }
        return result;
    }

private:
    mutable std::mutex mutex_{};
    std::unordered_map<std::string, Factory> factories_{};
};

}  // namespace acoustic
