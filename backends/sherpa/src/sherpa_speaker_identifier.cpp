#include <audition/backends/sherpa/speaker_identifier.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sherpa-onnx/c-api/cxx-api.h>

#include <audition/core/error.hpp>

namespace audition {
namespace {

std::string keyFor(SpeakerId id) {
    return "speaker:" + std::to_string(id.value());
}

void validateThreshold(Score threshold) {
    if (!std::isfinite(threshold.value) ||
        threshold.value < -1.0 || threshold.value > 1.0) {
        throw Error{ErrorCode::InvalidArgument,
                    "Sherpa speaker similarity threshold must be in [-1, 1]"};
    }
}

}  // namespace

class SherpaSpeakerIdentifier::Impl {
public:
    explicit Impl(SherpaSpeakerIdentifierOptions options)
        : options_(std::move(options)) {
        validateSherpaSpeakerIdentifierOptions(options_);
        rebuild();
    }

    void clear() {
        std::lock_guard<std::mutex> lock{mutex_};
        enrollments_.clear();
        rebuildUnlocked();
    }

    void enroll(const SpeakerEnrollment& enrollment) {
        if (!enrollment.speaker_id.valid()) {
            throw Error{ErrorCode::InvalidArgument,
                        "Speaker enrollment requires a valid SpeakerId"};
        }
        if (enrollment.embeddings.empty()) {
            throw Error{ErrorCode::InvalidArgument,
                        "Speaker enrollment requires at least one embedding"};
        }

        if (enrollment.embeddings.size() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Speaker enrollment count exceeds backend range"};
        }

        std::vector<float> flattened;
        flattened.reserve(enrollment.embeddings.size() * options_.embedding_dimension);
        for (const auto& embedding : enrollment.embeddings) {
            validateEmbedding(embedding);
            flattened.insert(flattened.end(),
                             embedding.values.begin(), embedding.values.end());
        }

        const auto count = static_cast<std::int32_t>(enrollment.embeddings.size());
        const auto key = keyFor(enrollment.speaker_id);

        std::lock_guard<std::mutex> lock{mutex_};
        if (enrollments_.find(key) != enrollments_.end()) {
            (void)manager_->Remove(key);
        }

        if (!manager_->AddListFlattened(key, flattened.data(), count)) {
            throw Error{ErrorCode::ProcessingError,
                        "Sherpa speaker manager rejected enrollment"};
        }
        enrollments_[key] = enrollment;
    }

    bool remove(SpeakerId speaker_id) {
        if (!speaker_id.valid()) {
            return false;
        }
        const auto key = keyFor(speaker_id);
        std::lock_guard<std::mutex> lock{mutex_};
        const auto erased = enrollments_.erase(key);
        const bool removed = manager_->Remove(key);
        return erased != 0U && removed;
    }

    [[nodiscard]] std::vector<SpeakerIdentity> identifyTopK(
        const SpeakerEmbedding& candidate,
        Score threshold,
        std::size_t max_results) const {
        validateThreshold(threshold);
        validateEmbedding(candidate);
        if (max_results == 0U) {
            return {};
        }
        if (max_results >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Requested speaker result count exceeds backend range"};
        }

        std::lock_guard<std::mutex> lock{mutex_};
        const auto matches = manager_->GetBestMatches(
            candidate.values.data(),
            static_cast<float>(threshold.value),
            static_cast<std::int32_t>(max_results));

        std::vector<SpeakerIdentity> result;
        result.reserve(matches.size());
        for (const auto& match : matches) {
            const auto it = enrollments_.find(match.name);
            if (it == enrollments_.end()) {
                throw Error{ErrorCode::ProcessingError,
                            "Sherpa speaker manager returned an unknown enrollment key"};
            }
            SpeakerIdentity identity{};
            identity.speaker_id = it->second.speaker_id;
            identity.display_name = it->second.display_name;
            identity.similarity = Score{static_cast<double>(match.score)};
            identity.calibrated_probability.reset();
            result.push_back(std::move(identity));
        }
        return result;
    }

    [[nodiscard]] std::optional<SpeakerIdentity> identify(
        const SpeakerEmbedding& candidate,
        Score threshold) const {
        auto matches = identifyTopK(candidate, threshold, 1U);
        if (matches.empty()) {
            return std::nullopt;
        }
        return std::move(matches.front());
    }

    void validateEmbedding(const SpeakerEmbedding& embedding) const {
        if (embedding.values.size() != options_.embedding_dimension) {
            throw Error{ErrorCode::InvalidArgument,
                        "Speaker embedding dimension does not match identification index"};
        }
        double norm2 = 0.0;
        for (const float value : embedding.values) {
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
        if (!options_.model_id.empty() && !embedding.model_id.empty() &&
            embedding.model_id != options_.model_id) {
            throw Error{ErrorCode::InvalidArgument,
                        "Speaker embedding model does not match identification index"};
        }
    }

    void rebuild() {
        std::lock_guard<std::mutex> lock{mutex_};
        rebuildUnlocked();
    }

    void rebuildUnlocked() {
        auto manager = sherpa_onnx::cxx::SpeakerEmbeddingManager::Create(
            static_cast<std::int32_t>(options_.embedding_dimension));
        if (!manager.Get()) {
            throw Error{ErrorCode::BackendUnavailable,
                        "Failed to create Sherpa speaker embedding manager"};
        }
        manager_ =
            std::make_unique<sherpa_onnx::cxx::SpeakerEmbeddingManager>(
                std::move(manager));
    }

    SherpaSpeakerIdentifierOptions options_{};
    std::unique_ptr<sherpa_onnx::cxx::SpeakerEmbeddingManager> manager_{};
    std::unordered_map<std::string, SpeakerEnrollment> enrollments_{};
    mutable std::mutex mutex_{};
};

SherpaSpeakerIdentifier::SherpaSpeakerIdentifier(
    SherpaSpeakerIdentifierOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaSpeakerIdentifier::~SherpaSpeakerIdentifier() = default;
SherpaSpeakerIdentifier::SherpaSpeakerIdentifier(SherpaSpeakerIdentifier&&) noexcept = default;
SherpaSpeakerIdentifier& SherpaSpeakerIdentifier::operator=(
    SherpaSpeakerIdentifier&&) noexcept = default;

BackendInfo SherpaSpeakerIdentifier::backendInfo() const {
    return {"sherpa-onnx-speaker-identifier", "99ddefaa"};
}
void SherpaSpeakerIdentifier::clear() { impl_->clear(); }
void SherpaSpeakerIdentifier::enroll(const SpeakerEnrollment& enrollment) {
    impl_->enroll(enrollment);
}
bool SherpaSpeakerIdentifier::remove(SpeakerId speaker_id) {
    return impl_->remove(speaker_id);
}
std::optional<SpeakerIdentity> SherpaSpeakerIdentifier::identify(
    const SpeakerEmbedding& candidate,
    Score threshold) const {
    return impl_->identify(candidate, threshold);
}
std::vector<SpeakerIdentity> SherpaSpeakerIdentifier::identifyTopK(
    const SpeakerEmbedding& candidate,
    Score threshold,
    std::size_t max_results) const {
    return impl_->identifyTopK(candidate, threshold, max_results);
}
const SherpaSpeakerIdentifierOptions& SherpaSpeakerIdentifier::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
