#include <audition/classify/vocabulary.hpp>

#include <algorithm>
#include <unordered_set>

#include <audition/core/error.hpp>

namespace audition {
namespace {
void validate(const VocabularySelection& selection) {
    if (selection.top_k == 0U) {
        throw Error{ErrorCode::InvalidArgument, "Vocabulary top_k must be positive"};
    }
    if (selection.mode == VocabularyMode::SelectedClasses && selection.labels.empty()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Selected vocabulary requires at least one label"};
    }
}
} // namespace

VocabularyResult selectVocabulary(
    const ClassificationResult& result,
    const VocabularySelection& selection,
    const std::vector<std::string>& supported_labels) {
    validate(selection);
    VocabularyResult output{};
    std::unordered_set<std::string> supported(supported_labels.begin(), supported_labels.end());
    std::unordered_set<std::string> wanted;
    if (selection.mode == VocabularyMode::SelectedClasses) {
        for (const auto& label : selection.labels) {
            if (!wanted.insert(label).second) continue;
            if (supported.find(label) == supported.end()) {
                output.unsupported_labels.push_back(label);
            }
        }
    }
    for (const auto& item : result.classes) {
        if (selection.mode == VocabularyMode::SelectedClasses &&
            (wanted.find(item.label) == wanted.end() ||
             supported.find(item.label) == supported.end())) {
            continue;
        }
        output.classes.push_back(
            {item.label, item.probability, VocabularyScoreKind::ModelProbability});
    }
    std::stable_sort(output.classes.begin(), output.classes.end(),
                     [](const VocabularyClass& a, const VocabularyClass& b) {
                         return a.probability.value() > b.probability.value();
                     });
    if (output.classes.size() > selection.top_k) output.classes.resize(selection.top_k);
    return output;
}

VocabularyResult classifyVocabulary(
    const IOpenVocabularyAudioClassifier& classifier,
    AudioView audio,
    const VocabularySelection& selection) {
    validate(selection);
    if (selection.mode != VocabularyMode::SelectedClasses) {
        throw Error{ErrorCode::InvalidArgument,
                    "Open vocabulary requires explicit candidate labels"};
    }
    auto raw = classifier.classify(audio, selection.labels);
    VocabularyResult output{};
    for (const auto& item : raw.classes) {
        output.classes.push_back(
            {item.label, item.probability, VocabularyScoreKind::RelativeSimilarity});
    }
    std::stable_sort(output.classes.begin(), output.classes.end(),
                     [](const VocabularyClass& a, const VocabularyClass& b) {
                         return a.probability.value() > b.probability.value();
                     });
    if (output.classes.size() > selection.top_k) output.classes.resize(selection.top_k);
    return output;
}
} // namespace audition
