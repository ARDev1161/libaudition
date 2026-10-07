#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <audition/core/execution.hpp>

namespace audition {

struct ModelLicense {
    std::string spdx_id{};
    bool commercial_use{false};
    bool redistribution{false};
    bool research_only{false};
};

struct ModelDescriptor {
    std::string id{};
    std::string version{};
    std::string task{};
    std::string backend{};
    std::filesystem::path artifact_path{};
    std::string source_uri{};
    std::string source_revision{};
    std::string sha256{};
    ModelLicense license{};
    std::vector<std::string> capabilities{};
    std::vector<std::string> supported_providers{};
    std::map<std::string, std::string> metadata{};
};

}  // namespace audition
