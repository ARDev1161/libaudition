#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include <audition/interfaces/model_registry.hpp>

namespace audition {

class InMemoryModelRegistry final : public IModelRegistry {
public:
    void add(ModelDescriptor descriptor) override;
    [[nodiscard]] std::optional<ModelDescriptor> find(const std::string& id) const override;
    [[nodiscard]] std::vector<ModelDescriptor> list() const override;

private:
    mutable std::mutex mutex_{};
    std::unordered_map<std::string, ModelDescriptor> models_{};
};

}  // namespace audition
