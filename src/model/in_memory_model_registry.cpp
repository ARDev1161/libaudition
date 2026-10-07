#include <audition/model/in_memory_model_registry.hpp>

#include <audition/core/error.hpp>

namespace audition {

void InMemoryModelRegistry::add(ModelDescriptor descriptor) {
    if (descriptor.id.empty()) {
        throw Error{ErrorCode::InvalidArgument, "Model descriptor requires a non-empty id"};
    }
    std::lock_guard<std::mutex> lock{mutex_};
    models_[descriptor.id] = std::move(descriptor);
}

std::optional<ModelDescriptor> InMemoryModelRegistry::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto it = models_.find(id);
    if (it == models_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ModelDescriptor> InMemoryModelRegistry::list() const {
    std::lock_guard<std::mutex> lock{mutex_};
    std::vector<ModelDescriptor> result;
    result.reserve(models_.size());
    for (const auto& entry : models_) {
        result.push_back(entry.second);
    }
    return result;
}

}  // namespace audition
