#pragma once

#include <optional>
#include <string>
#include <vector>

#include <audition/model/model_descriptor.hpp>

namespace audition {

class IModelRegistry {
public:
    virtual ~IModelRegistry() = default;
    virtual void add(ModelDescriptor descriptor) = 0;
    [[nodiscard]] virtual std::optional<ModelDescriptor> find(const std::string& id) const = 0;
    [[nodiscard]] virtual std::vector<ModelDescriptor> list() const = 0;
};

}  // namespace audition
