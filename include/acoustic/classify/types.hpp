#pragma once

#include <string>
#include <vector>

#include <acoustic/core/probability.hpp>

namespace acoustic {

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
    Probability quality{Probability::zero()};
};

}  // namespace acoustic
