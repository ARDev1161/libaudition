#pragma once

#include <memory>
#include <vector>
#include <string>

#include <audition/backends/clap/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/classify.hpp>

namespace audition {

class AUDITION_API ClapOpenVocabularyClassifier final
    : public IOpenVocabularyAudioClassifier {
public:
    explicit ClapOpenVocabularyClassifier(
        ClapOpenVocabularyOptions options);
    ~ClapOpenVocabularyClassifier() override;

    ClapOpenVocabularyClassifier(
        const ClapOpenVocabularyClassifier&) = delete;
    ClapOpenVocabularyClassifier& operator=(
        const ClapOpenVocabularyClassifier&) = delete;
    ClapOpenVocabularyClassifier(
        ClapOpenVocabularyClassifier&&) noexcept;
    ClapOpenVocabularyClassifier& operator=(
        ClapOpenVocabularyClassifier&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] ClassifierCapabilities capabilities() const override;
    [[nodiscard]] ClassificationResult classify(
        AudioView audio,
        const std::vector<std::string>& candidate_labels) const override;

    [[nodiscard]] const ClapOpenVocabularyOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
