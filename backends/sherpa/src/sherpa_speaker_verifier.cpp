#include <audition/backends/sherpa/speaker_verifier.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <sherpa-onnx/c-api/cxx-api.h>

#include <audition/core/error.hpp>

namespace audition {
namespace {

void validateThreshold(Score threshold) {
    if (!std::isfinite(threshold.value) ||
        threshold.value < -1.0 || threshold.value > 1.0) {
        throw Error{ErrorCode::InvalidArgument,
                    "Sherpa speaker similarity threshold must be in [-1, 1]"};
    }
}

void validateVector(const std::vector<float>& values) {
    if (values.empty() ||
        values.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw Error{ErrorCode::InvalidArgument,
                    "Speaker embedding dimension is empty or exceeds backend range"};
    }
    double norm2 = 0.0;
    for (const float value : values) {
        if (!std::isfinite(value)) {
            throw Error{ErrorCode::InvalidArgument,
                        "Speaker embedding contains non-finite values"};
        }
        norm2 += static_cast<double>(value) * static_cast<double>(value);
    }
    if (!(norm2 > 0.0) || !std::isfinite(norm2)) {
        throw Error{ErrorCode::InvalidArgument,
                    "Speaker embedding must have a finite non-zero norm"};
    }
}

void validateEmbeddingPair(const SpeakerEmbedding& reference,
                           const SpeakerEmbedding& candidate,
                           bool require_same_model_id) {
    validateVector(reference.values);
    validateVector(candidate.values);
    if (reference.values.size() != candidate.values.size()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Speaker embedding dimensions must match"};
    }
    if (require_same_model_id &&
        !reference.model_id.empty() && !candidate.model_id.empty() &&
        reference.model_id != candidate.model_id) {
        throw Error{ErrorCode::InvalidArgument,
                    "Speaker embeddings were produced by different models"};
    }
}

}  // namespace

class SherpaSpeakerVerifier::Impl {
public:
    explicit Impl(SherpaSpeakerVerifierOptions options) : options_(options) {}

    [[nodiscard]] SpeakerVerificationResult compare(
        const SpeakerEmbedding& reference,
        const SpeakerEmbedding& candidate,
        Score threshold) const {
        validateThreshold(threshold);
        validateEmbeddingPair(reference, candidate, options_.require_same_model_id);

        const auto dim = static_cast<std::int32_t>(reference.values.size());
        auto manager = sherpa_onnx::cxx::SpeakerEmbeddingManager::Create(dim);
        if (!manager.Get()) {
            throw Error{ErrorCode::BackendUnavailable,
                        "Failed to create Sherpa speaker embedding manager"};
        }
        constexpr const char* kReference = "__reference__";
        if (!manager.Add(kReference, reference.values.data())) {
            throw Error{ErrorCode::ProcessingError,
                        "Failed to enroll reference speaker embedding"};
        }

        const auto matches = manager.GetBestMatches(candidate.values.data(), -1.0F, 1);
        if (matches.empty()) {
            throw Error{ErrorCode::ProcessingError,
                        "Sherpa speaker manager did not return a similarity score"};
        }

        SpeakerVerificationResult result{};
        result.similarity = Score{static_cast<double>(matches.front().score)};
        result.matched = result.similarity.value >= threshold.value;
        result.calibrated_probability.reset();
        return result;
    }

    SherpaSpeakerVerifierOptions options_{};
};

SherpaSpeakerVerifier::SherpaSpeakerVerifier(SherpaSpeakerVerifierOptions options)
    : impl_(std::make_unique<Impl>(options)) {}
SherpaSpeakerVerifier::~SherpaSpeakerVerifier() = default;
SherpaSpeakerVerifier::SherpaSpeakerVerifier(SherpaSpeakerVerifier&&) noexcept = default;
SherpaSpeakerVerifier& SherpaSpeakerVerifier::operator=(SherpaSpeakerVerifier&&) noexcept = default;

BackendInfo SherpaSpeakerVerifier::backendInfo() const {
    return {"sherpa-onnx-speaker-verifier", "99ddefaa"};
}
SpeakerVerificationResult SherpaSpeakerVerifier::compare(
    const SpeakerEmbedding& reference,
    const SpeakerEmbedding& candidate,
    Score threshold) const {
    return impl_->compare(reference, candidate, threshold);
}

}  // namespace audition
