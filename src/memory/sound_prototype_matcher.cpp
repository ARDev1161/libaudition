#include <audition/memory/sound_prototype_matcher.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <audition/core/error.hpp>

namespace audition {
namespace {

void validateQuery(const AudioEmbedding& embedding) {
    if (embedding.model_id.empty()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Audio embedding model_id must not be empty"};
    }
    if (embedding.values.empty()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Audio embedding values must not be empty"};
    }

    double squared_norm = 0.0;
    for (float value : embedding.values) {
        if (!std::isfinite(value)) {
            throw Error{ErrorCode::InvalidArgument,
                        "Audio embedding values must be finite"};
        }
        squared_norm += static_cast<double>(value) *
                        static_cast<double>(value);
    }
    if (!std::isfinite(squared_norm) ||
        squared_norm <= std::numeric_limits<double>::epsilon()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Audio embedding must have non-zero finite norm"};
    }
}

std::vector<double> normalized(const std::vector<float>& values) {
    double squared_norm = 0.0;
    for (float value : values) {
        if (!std::isfinite(value)) {
            return {};
        }
        squared_norm += static_cast<double>(value) *
                        static_cast<double>(value);
    }
    if (!std::isfinite(squared_norm) ||
        squared_norm <= std::numeric_limits<double>::epsilon()) {
        return {};
    }

    const double inverse_norm = 1.0 / std::sqrt(squared_norm);
    std::vector<double> result;
    result.reserve(values.size());
    for (float value : values) {
        result.push_back(static_cast<double>(value) * inverse_norm);
    }
    return result;
}

std::optional<double> prototypeSimilarity(
    const AudioEmbedding& query,
    const std::vector<double>& normalized_query,
    const SoundPrototype& prototype) {
    std::vector<double> centroid(query.values.size(), 0.0);
    std::size_t count = 0U;

    for (const auto& example : prototype.examples) {
        if (example.model_id != query.model_id ||
            example.values.size() != query.values.size()) {
            continue;
        }

        const auto unit = normalized(example.values);
        if (unit.empty()) {
            continue;
        }
        for (std::size_t i = 0; i < unit.size(); ++i) {
            centroid[i] += unit[i];
        }
        ++count;
    }

    if (count == 0U) {
        return std::nullopt;
    }

    double centroid_squared_norm = 0.0;
    for (double value : centroid) {
        centroid_squared_norm += value * value;
    }
    if (!std::isfinite(centroid_squared_norm) ||
        centroid_squared_norm <= std::numeric_limits<double>::epsilon()) {
        return std::nullopt;
    }

    const double inverse_centroid_norm =
        1.0 / std::sqrt(centroid_squared_norm);
    double similarity = 0.0;
    for (std::size_t i = 0; i < centroid.size(); ++i) {
        similarity += normalized_query[i] *
                      centroid[i] * inverse_centroid_norm;
    }

    if (!std::isfinite(similarity)) {
        return std::nullopt;
    }
    return std::clamp(similarity, -1.0, 1.0);
}

void validateOptions(const SoundPrototypeMatchOptions& options) {
    if (!options.min_similarity.has_value()) {
        return;
    }

    const double threshold = options.min_similarity->value;
    if (!std::isfinite(threshold) || threshold < -1.0 || threshold > 1.0) {
        throw Error{ErrorCode::InvalidArgument,
                    "Cosine similarity threshold must be finite and in [-1, 1]"};
    }
}

}  // namespace

CosineSoundPrototypeMatcher::CosineSoundPrototypeMatcher(
    const ISoundPrototypeRegistry& registry) noexcept
    : registry_(&registry) {}

std::vector<SoundPrototypeMatch> CosineSoundPrototypeMatcher::match(
    const AudioEmbedding& query,
    const SoundPrototypeMatchOptions& options) const {
    validateQuery(query);
    validateOptions(options);

    const auto normalized_query = normalized(query.values);
    std::vector<SoundPrototypeMatch> result;

    for (const auto& prototype : registry_->list()) {
        const auto similarity =
            prototypeSimilarity(query, normalized_query, prototype);
        if (!similarity.has_value()) {
            continue;
        }
        if (options.min_similarity.has_value() &&
            *similarity < options.min_similarity->value) {
            continue;
        }

        result.push_back(
            {prototype.prototype_id, prototype.label, Score{*similarity}});
    }

    std::sort(result.begin(), result.end(),
              [](const SoundPrototypeMatch& lhs,
                 const SoundPrototypeMatch& rhs) {
                  if (lhs.similarity.value != rhs.similarity.value) {
                      return lhs.similarity.value > rhs.similarity.value;
                  }
                  return lhs.prototype_id.value() < rhs.prototype_id.value();
              });

    if (result.size() > options.max_results) {
        result.resize(options.max_results);
    }
    return result;
}

}  // namespace audition
