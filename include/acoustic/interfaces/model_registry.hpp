#pragma once

#include <optional>
#include <string>
#include <vector>

#include <acoustic/model/model_descriptor.hpp>

namespace acoustic {

class IModelRegistry {
public:
    virtual ~IModelRegistry() = default;
    virtual void add(ModelDescriptor descriptor) = 0;
    [[nodiscard]] virtual std::optional<ModelDescriptor> find(const std::string& id) const = 0;
    [[nodiscard]] virtual std::vector<ModelDescriptor> list() const = 0;
};

}  // namespace acoustic
