#include <audition/backends/clap/open_vocabulary_classifier.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <audition/backends/clap/audio_embedder.hpp>
#include <audition/core/error.hpp>

#include "detail/clap_text_encoder.hpp"

namespace audition {
namespace {

void requireConfiguration(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

ClapOpenVocabularyOptions validatedOptions(
    ClapOpenVocabularyOptions options) {
    validateClapOnnxAudioOptions(options.audio);
    validateClapOnnxTextOptions(options.text);

    requireConfiguration(
        options.audio.embedding_dimension ==
            options.text.embedding_dimension,
        "CLAP audio/text embedding dimensions must match");
    requireConfiguration(
        options.audio.model_id == options.text.model_id,
        "CLAP audio/text model_id values must match");
    requireConfiguration(
        std::isfinite(options.similarity_temperature) &&
            options.similarity_temperature > 0.0,
        "CLAP candidate similarity_temperature must be finite and positive");

    return options;
}

void validateCandidates(
    const std::vector<std::string>& candidate_labels) {
    if (candidate_labels.empty()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "CLAP open-vocabulary classification requires at least one candidate label"};
    }

    std::unordered_set<std::string> unique;
    unique.reserve(candidate_labels.size());

    for (const auto& label : candidate_labels) {
        if (label.empty()) {
            throw Error{
                ErrorCode::InvalidArgument,
                "CLAP candidate labels must not be empty"};
        }
        for (unsigned char byte : label) {
            if (byte >= 0x80U) {
                throw Error{
                    ErrorCode::UnsupportedFormat,
                    "CLAP candidate labels currently require ASCII text"};
            }
        }
        if (!unique.insert(label).second) {
            throw Error{
                ErrorCode::InvalidArgument,
                "CLAP candidate labels must be unique"};
        }
    }
}

double cosineSimilarity(
    const std::vector<float>& first,
    const std::vector<float>& second) {
    if (first.empty() || first.size() != second.size()) {
        throw Error{
            ErrorCode::ProcessingError,
            "CLAP audio/text embeddings have incompatible dimensions"};
    }

    double dot = 0.0;
    double first_norm = 0.0;
    double second_norm = 0.0;

    for (std::size_t i = 0U; i < first.size(); ++i) {
        const auto a = static_cast<double>(first[i]);
        const auto b = static_cast<double>(second[i]);
        if (!std::isfinite(a) || !std::isfinite(b)) {
            throw Error{
                ErrorCode::ProcessingError,
                "CLAP embedding contains a non-finite value"};
        }

        dot += a * b;
        first_norm += a * a;
        second_norm += b * b;
    }

    if (!std::isfinite(dot) ||
        !std::isfinite(first_norm) ||
        !std::isfinite(second_norm) ||
        first_norm <= std::numeric_limits<double>::epsilon() ||
        second_norm <= std::numeric_limits<double>::epsilon()) {
        throw Error{
            ErrorCode::ProcessingError,
            "CLAP embedding norm is zero or invalid"};
    }

    return dot / std::sqrt(first_norm * second_norm);
}

std::vector<double> candidateProbabilities(
    const std::vector<double>& similarities,
    double temperature) {
    if (similarities.empty()) {
        return {};
    }

    std::vector<double> scaled;
    scaled.reserve(similarities.size());

    double maximum =
        -std::numeric_limits<double>::infinity();
    for (const double similarity : similarities) {
        if (!std::isfinite(similarity)) {
            throw Error{
                ErrorCode::ProcessingError,
                "CLAP similarity is non-finite"};
        }
        const double value = similarity / temperature;
        scaled.push_back(value);
        maximum = std::max(maximum, value);
    }

    double sum = 0.0;
    for (double& value : scaled) {
        value = std::exp(value - maximum);
        sum += value;
    }

    if (!std::isfinite(sum) ||
        sum <= std::numeric_limits<double>::epsilon()) {
        throw Error{
            ErrorCode::ProcessingError,
            "CLAP candidate normalization failed"};
    }

    for (double& value : scaled) {
        value /= sum;
    }
    return scaled;
}

}  // namespace

void validateClapOpenVocabularyOptions(
    const ClapOpenVocabularyOptions& options) {
    static_cast<void>(
        validatedOptions(options));
}

class ClapOpenVocabularyClassifier::Impl {
public:
    explicit Impl(ClapOpenVocabularyOptions options)
        : options_(validatedOptions(std::move(options))),
          audio_(options_.audio),
          text_(options_.text) {}

    [[nodiscard]] ClassificationResult classify(
        AudioView audio,
        const std::vector<std::string>& candidate_labels) const {
        validateCandidates(candidate_labels);

        const auto audio_embedding =
            audio_.embed(audio);

        if (audio_embedding.model_id !=
            options_.audio.model_id) {
            throw Error{
                ErrorCode::ProcessingError,
                "CLAP audio embedding model_id changed at runtime"};
        }

        std::vector<double> similarities;
        similarities.reserve(candidate_labels.size());

        for (const auto& label : candidate_labels) {
            const auto text_embedding =
                text_.embed(label);
            similarities.push_back(
                cosineSimilarity(
                    audio_embedding.values,
                    text_embedding));
        }

        const auto probabilities =
            candidateProbabilities(
                similarities,
                options_.similarity_temperature);

        ClassificationResult result{};
        result.classes.reserve(candidate_labels.size());

        for (std::size_t i = 0U;
             i < candidate_labels.size();
             ++i) {
            result.classes.push_back(
                {candidate_labels[i],
                 Probability::from(
                     std::clamp(
                         probabilities[i],
                         0.0,
                         1.0))});
        }

        std::stable_sort(
            result.classes.begin(),
            result.classes.end(),
            [](const ClassScore& first,
               const ClassScore& second) {
                return first.probability.value() >
                       second.probability.value();
            });

        return result;
    }

    ClapOpenVocabularyOptions options_{};
    ClapAudioEmbedder audio_;
    clap_detail::ClapTextEncoder text_;
};

ClapOpenVocabularyClassifier::
    ClapOpenVocabularyClassifier(
        ClapOpenVocabularyOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(options))) {}

ClapOpenVocabularyClassifier::
    ~ClapOpenVocabularyClassifier() = default;

ClapOpenVocabularyClassifier::
    ClapOpenVocabularyClassifier(
        ClapOpenVocabularyClassifier&&) noexcept = default;

ClapOpenVocabularyClassifier&
ClapOpenVocabularyClassifier::operator=(
    ClapOpenVocabularyClassifier&&) noexcept = default;

BackendInfo
ClapOpenVocabularyClassifier::backendInfo() const {
    return impl_->audio_.backendInfo();
}

ClassifierCapabilities
ClapOpenVocabularyClassifier::capabilities() const {
    const auto embedding =
        impl_->audio_.capabilities();

    ClassifierCapabilities capabilities{};
    capabilities.open_vocabulary = true;
    capabilities.embeddings = true;
    capabilities.audio = embedding.audio;
    capabilities.execution = embedding.execution;
    return capabilities;
}

ClassificationResult
ClapOpenVocabularyClassifier::classify(
    AudioView audio,
    const std::vector<std::string>& candidate_labels) const {
    return impl_->classify(
        audio, candidate_labels);
}

const ClapOpenVocabularyOptions&
ClapOpenVocabularyClassifier::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
