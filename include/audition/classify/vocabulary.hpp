#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <audition/core/export.hpp>
#include <audition/interfaces/classify.hpp>

namespace audition {

// Score semantics are preserved: model probabilities remain probabilities,
// while candidate-relative CLAP values must not be compared numerically with them.
enum class VocabularyScoreKind { ModelProbability, RelativeSimilarity };
enum class VocabularyMode { AllClasses, SelectedClasses };

struct VocabularySelection {
    VocabularyMode mode{VocabularyMode::AllClasses};
    std::vector<std::string> labels{};
    std::size_t top_k{5U};
};

struct VocabularyClass {
    std::string label{};
    Probability probability{Probability::zero()};
    VocabularyScoreKind score_kind{VocabularyScoreKind::ModelProbability};
};

struct VocabularyResult {
    std::vector<VocabularyClass> classes{};
    std::vector<std::string> unsupported_labels{};
};

// Filtering happens after full closed-vocabulary inference. No label is
// fabricated when the model doesn't support it. Explicit catalog is mandatory
// for precise unsupported-label validation (a Top-K alone is not a catalog).
AUDITION_API VocabularyResult selectVocabulary(
    const ClassificationResult& result,
    const VocabularySelection& selection,
    const std::vector<std::string>& supported_labels);

// Open-vocabulary classifier candidates participate in inference.
AUDITION_API VocabularyResult classifyVocabulary(
    const IOpenVocabularyAudioClassifier& classifier,
    AudioView audio,
    const VocabularySelection& selection);

} // namespace audition
