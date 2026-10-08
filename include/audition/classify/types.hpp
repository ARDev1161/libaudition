#pragma once

#include <optional>
#include <string>
#include <vector>

#include <audition/core/probability.hpp>

namespace audition {

struct ClassScore {
    std::string label{};
    Probability probability{Probability::zero()};
};

struct ClassificationResult {
    std::vector<ClassScore> classes{};
};

struct AudioEmbedding {
    std::string model_id{};
    std::vector<float> values{};
    std::optional<Probability> quality{};
};

}  // namespace audition
