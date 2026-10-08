#include <audition/memory/source_identity_resolver.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

namespace audition {
namespace {

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finiteVec(const Vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

void validateFingerprint(const SourceFingerprint& fingerprint) {
    if (fingerprint.model_id.empty()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source fingerprint model_id must not be empty"};
    }
    if (fingerprint.embedding.empty()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source fingerprint embedding must not be empty"};
    }

    double squared_norm = 0.0;
    for (const float value : fingerprint.embedding) {
        if (!std::isfinite(value)) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Source fingerprint embedding must contain only finite values"};
        }
        squared_norm +=
            static_cast<double>(value) * static_cast<double>(value);
    }

    if (!finite(squared_norm) ||
        squared_norm <= std::numeric_limits<double>::epsilon()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source fingerprint embedding must have non-zero finite norm"};
    }
}

[[nodiscard]] std::optional<double> cosineSimilarity(
    const SourceFingerprint& lhs,
    const SourceFingerprint& rhs) {
    if (lhs.model_id != rhs.model_id ||
        lhs.embedding.size() != rhs.embedding.size() ||
        lhs.embedding.empty()) {
        return std::nullopt;
    }

    double dot = 0.0;
    double lhs_norm2 = 0.0;
    double rhs_norm2 = 0.0;

    for (std::size_t index = 0; index < lhs.embedding.size(); ++index) {
        const double left = static_cast<double>(lhs.embedding[index]);
        const double right = static_cast<double>(rhs.embedding[index]);
        if (!finite(left) || !finite(right)) {
            return std::nullopt;
        }
        dot += left * right;
        lhs_norm2 += left * left;
        rhs_norm2 += right * right;
    }

    if (!finite(dot) || !finite(lhs_norm2) || !finite(rhs_norm2) ||
        lhs_norm2 <= std::numeric_limits<double>::epsilon() ||
        rhs_norm2 <= std::numeric_limits<double>::epsilon()) {
        return std::nullopt;
    }

    return std::clamp(
        dot / std::sqrt(lhs_norm2 * rhs_norm2),
        -1.0,
        1.0);
}

struct FingerprintEvidence {
    bool has_compatible{false};
    std::optional<double> best_similarity{};
};

[[nodiscard]] FingerprintEvidence fingerprintEvidence(
    const SourceFingerprint& query,
    const AcousticSourceProfile& profile) {
    FingerprintEvidence evidence{};

    for (const auto& stored : profile.fingerprints) {
        if (stored.model_id != query.model_id ||
            stored.embedding.size() != query.embedding.size()) {
            continue;
        }

        evidence.has_compatible = true;
        const auto similarity = cosineSimilarity(query, stored);
        if (!similarity.has_value()) {
            continue;
        }
        if (!evidence.best_similarity.has_value() ||
            *similarity > *evidence.best_similarity) {
            evidence.best_similarity = *similarity;
        }
    }

    return evidence;
}

[[nodiscard]] double distance(const Vec3& lhs, const Vec3& rhs) noexcept {
    const double dx = lhs.x - rhs.x;
    const double dy = lhs.y - rhs.y;
    const double dz = lhs.z - rhs.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

[[nodiscard]] double directionAngle(
    const Direction3D& lhs,
    const Direction3D& rhs) noexcept {
    const Vec3& left = lhs.vector();
    const Vec3& right = rhs.vector();
    const double dot =
        left.x * right.x +
        left.y * right.y +
        left.z * right.z;
    return std::acos(std::clamp(dot, -1.0, 1.0));
}

void validateObservation(const SourceIdentityObservation& observation) {
    if (!observation.track_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source identity observation requires a valid track ID"};
    }

    if (observation.range.has_value()) {
        if (!observation.range->distance_m.valid() ||
            observation.range->distance_m.mean < 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Source identity range must be finite and non-negative"};
        }
    }

    if (observation.position.has_value() &&
        !finiteVec(observation.position->mean_m)) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Source identity position must contain only finite coordinates"};
    }

    if (observation.fingerprint.has_value()) {
        validateFingerprint(*observation.fingerprint);
    }
}

void validateOptions(const HeuristicSourceIdentityResolverOptions& options) {
    if (options.max_reacquisition_age.nanoseconds() <= 0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Identity max_reacquisition_age must be positive"};
    }

    const double positive_values[] = {
        options.max_position_distance_m,
        options.max_range_delta_m,
        options.max_direction_angle_rad};

    for (const double value : positive_values) {
        if (!finite(value) || value <= 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Identity geometry thresholds must be finite and positive"};
        }
    }

    const double unit_interval_values[] = {
        options.min_fingerprint_quality,
        options.association_threshold};

    for (const double value : unit_interval_values) {
        if (!finite(value) || value < 0.0 || value > 1.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Identity probability-like thresholds must be in [0, 1]"};
        }
    }

    if (!finite(options.min_fingerprint_cosine_similarity) ||
        options.min_fingerprint_cosine_similarity < -1.0 ||
        options.min_fingerprint_cosine_similarity > 1.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Fingerprint cosine threshold must be in [-1, 1]"};
    }

    const double weights[] = {
        options.fingerprint_weight,
        options.position_weight,
        options.range_weight,
        options.direction_weight};

    double total_weight = 0.0;
    for (const double weight : weights) {
        if (!finite(weight) || weight < 0.0) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Identity evidence weights must be finite and non-negative"};
        }
        total_weight += weight;
    }

    if (total_weight <= 0.0) {
        throw Error{
            ErrorCode::InvalidArgument,
            "At least one identity evidence weight must be positive"};
    }

    if (options.max_active_tracks == 0U ||
        options.max_recent_sources == 0U ||
        options.max_recent_sources < options.max_active_tracks) {
        throw Error{
            ErrorCode::InvalidArgument,
            "Identity state limits must be positive and recent_sources >= active_tracks"};
    }
}

}  // namespace

HeuristicSourceIdentityResolver::HeuristicSourceIdentityResolver(
    IAcousticSourceRegistry& registry,
    HeuristicSourceIdentityResolverOptions options)
    : registry_(&registry),
      options_(options) {
    validateOptions(options_);
}

void HeuristicSourceIdentityResolver::reset() {
    last_timestamp_.reset();
    active_tracks_.clear();
    recent_sources_.clear();
}

void HeuristicSourceIdentityResolver::validateAndAdvanceTimestamp(
    Timestamp timestamp) {
    if (last_timestamp_.has_value()) {
        if (!timestamp.comparableWith(*last_timestamp_)) {
            throw Error{
                ErrorCode::ClockDomainMismatch,
                "Source identity resolver requires one clock identity per state epoch"};
        }
        if (timestamp.nanoseconds() < last_timestamp_->nanoseconds()) {
            throw Error{
                ErrorCode::InvalidArgument,
                "Source identity observations must be globally time ordered"};
        }
    }
    last_timestamp_ = timestamp;
}

void HeuristicSourceIdentityResolver::pruneRecentSources(
    Timestamp timestamp) {
    const std::int64_t max_age_ns =
        options_.max_reacquisition_age.nanoseconds();

    const auto sourceIsActive = [this](AcousticSourceId source_id) {
        for (const auto& item : active_tracks_) {
            if (item.second.source_id == source_id) {
                return true;
            }
        }
        return false;
    };

    for (auto it = recent_sources_.begin(); it != recent_sources_.end();) {
        const std::int64_t age_ns =
            timestamp.nanoseconds() - it->second.last_seen.nanoseconds();
        if (!sourceIsActive(it->first) && age_ns > max_age_ns) {
            it = recent_sources_.erase(it);
        } else {
            ++it;
        }
    }

    while (recent_sources_.size() > options_.max_recent_sources) {
        auto oldest = recent_sources_.end();
        for (auto it = recent_sources_.begin();
             it != recent_sources_.end();
             ++it) {
            if (sourceIsActive(it->first)) {
                continue;
            }
            if (oldest == recent_sources_.end() ||
                it->second.last_seen.nanoseconds() <
                    oldest->second.last_seen.nanoseconds() ||
                (it->second.last_seen.nanoseconds() ==
                     oldest->second.last_seen.nanoseconds() &&
                 it->first.value() < oldest->first.value())) {
                oldest = it;
            }
        }

        if (oldest == recent_sources_.end()) {
            break;
        }
        recent_sources_.erase(oldest);
    }
}

void HeuristicSourceIdentityResolver::updateSourceState(
    AcousticSourceId source_id,
    const SourceIdentityObservation& observation) {
    SourceState& state = recent_sources_[source_id];
    state.last_seen = observation.timestamp;
    state.direction = observation.direction;
    state.range = observation.range;
    state.position = observation.position;

    pruneRecentSources(observation.timestamp);
}

void HeuristicSourceIdentityResolver::maybeStoreFingerprint(
    AcousticSourceId source_id,
    const SourceIdentityObservation& observation) {
    if (!observation.fingerprint.has_value() ||
        observation.fingerprint->quality.value() <
            options_.min_fingerprint_quality) {
        return;
    }

    const auto profile = registry_->find(source_id);
    if (!profile.has_value()) {
        throw Error{
            ErrorCode::InvalidState,
            "Resolved acoustic source is missing from the registry"};
    }

    for (const auto& stored : profile->fingerprints) {
        if (stored.model_id == observation.fingerprint->model_id) {
            return;
        }
    }

    registry_->addFingerprint(source_id, *observation.fingerprint);
}

SourceIdentityDecision HeuristicSourceIdentityResolver::observe(
    const SourceIdentityObservation& observation) {
    validateObservation(observation);
    validateAndAdvanceTimestamp(observation.timestamp);
    pruneRecentSources(observation.timestamp);

    const auto active = active_tracks_.find(observation.track_id);
    if (active != active_tracks_.end()) {
        active->second.last_seen = observation.timestamp;
        updateSourceState(active->second.source_id, observation);
        maybeStoreFingerprint(active->second.source_id, observation);
        return {
            active->second.source_id,
            Probability::one(),
            false};
    }

    if (active_tracks_.size() >= options_.max_active_tracks) {
        throw Error{
            ErrorCode::InvalidState,
            "Source identity resolver active-track capacity exceeded"};
    }

    const auto sourceIsActive = [this](AcousticSourceId source_id) {
        for (const auto& item : active_tracks_) {
            if (item.second.source_id == source_id) {
                return true;
            }
        }
        return false;
    };

    auto profiles = registry_->list();
    std::sort(
        profiles.begin(),
        profiles.end(),
        [](const AcousticSourceProfile& lhs,
           const AcousticSourceProfile& rhs) {
            return lhs.source_id.value() < rhs.source_id.value();
        });

    struct Candidate {
        AcousticSourceId source_id{};
        double score{0.0};
    };
    std::optional<Candidate> best{};

    const bool use_query_fingerprint =
        observation.fingerprint.has_value() &&
        observation.fingerprint->quality.value() >=
            options_.min_fingerprint_quality;

    for (const auto& profile : profiles) {
        if (!profile.source_id.valid() ||
            sourceIsActive(profile.source_id)) {
            continue;
        }

        double weighted_score = 0.0;
        double available_weight = 0.0;
        bool fingerprint_anchor = false;
        bool position_anchor = false;
        bool range_anchor = false;
        bool direction_anchor = false;

        if (use_query_fingerprint) {
            const auto evidence =
                fingerprintEvidence(*observation.fingerprint, profile);

            if (evidence.has_compatible) {
                if (!evidence.best_similarity.has_value() ||
                    *evidence.best_similarity <
                        options_.min_fingerprint_cosine_similarity) {
                    continue;
                }

                const double fingerprint_score =
                    std::clamp(*evidence.best_similarity, 0.0, 1.0);
                weighted_score +=
                    options_.fingerprint_weight * fingerprint_score;
                available_weight += options_.fingerprint_weight;
                fingerprint_anchor = true;
            }
        }

        const auto state_it = recent_sources_.find(profile.source_id);
        if (state_it != recent_sources_.end()) {
            const SourceState& state = state_it->second;
            const std::int64_t age_ns =
                observation.timestamp.nanoseconds() -
                state.last_seen.nanoseconds();
            const double recency = std::clamp(
                1.0 -
                    static_cast<double>(age_ns) /
                        static_cast<double>(
                            options_.max_reacquisition_age.nanoseconds()),
                0.0,
                1.0);

            if (observation.position.has_value() &&
                state.position.has_value()) {
                const double delta = distance(
                    observation.position->mean_m,
                    state.position->mean_m);
                if (finite(delta) &&
                    delta <= options_.max_position_distance_m) {
                    const double score =
                        (1.0 -
                         delta / options_.max_position_distance_m) *
                        recency;
                    weighted_score +=
                        options_.position_weight * score;
                    available_weight += options_.position_weight;
                    position_anchor = true;
                }
            }

            if (observation.range.has_value() &&
                state.range.has_value()) {
                const double delta = std::abs(
                    observation.range->distance_m.mean -
                    state.range->distance_m.mean);
                if (finite(delta) &&
                    delta <= options_.max_range_delta_m) {
                    const double score =
                        (1.0 - delta / options_.max_range_delta_m) *
                        recency;
                    weighted_score +=
                        options_.range_weight * score;
                    available_weight += options_.range_weight;
                    range_anchor = true;
                }
            }

            const double angle = directionAngle(
                observation.direction.direction,
                state.direction->direction);
            if (finite(angle) &&
                angle <= options_.max_direction_angle_rad) {
                const double score =
                    (1.0 -
                     angle / options_.max_direction_angle_rad) *
                    recency;
                weighted_score +=
                    options_.direction_weight * score;
                available_weight += options_.direction_weight;
                direction_anchor = true;
            }
        }

        // DirectionEstimate has no sensor-pose/frame metadata in the identity
        // contract, so direction/range may refine an already anchored candidate
        // but cannot establish cross-track identity by themselves.
        const bool geometry_anchor = position_anchor;

        if (!fingerprint_anchor && !geometry_anchor) {
            continue;
        }
        if (available_weight <= 0.0) {
            continue;
        }

        const double score = std::clamp(
            weighted_score / available_weight,
            0.0,
            1.0);
        if (score < options_.association_threshold) {
            continue;
        }

        if (!best.has_value() ||
            score > best->score ||
            (score == best->score &&
             profile.source_id.value() < best->source_id.value())) {
            best = Candidate{profile.source_id, score};
        }
    }

    AcousticSourceId source_id{};
    bool newly_created = false;
    Probability confidence = Probability::zero();

    if (best.has_value()) {
        source_id = best->source_id;
        confidence = Probability::from(best->score);
    } else {
        source_id = registry_->create();
        newly_created = true;
    }

    active_tracks_.emplace(
        observation.track_id,
        TrackBinding{source_id, observation.timestamp});
    updateSourceState(source_id, observation);
    maybeStoreFingerprint(source_id, observation);

    return {source_id, confidence, newly_created};
}

void HeuristicSourceIdentityResolver::endTrack(
    SpatialTrackId track_id,
    Timestamp timestamp) {
    if (!track_id.valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "endTrack requires a valid track ID"};
    }

    validateAndAdvanceTimestamp(timestamp);
    pruneRecentSources(timestamp);

    const auto it = active_tracks_.find(track_id);
    if (it == active_tracks_.end()) {
        return;
    }

    const AcousticSourceId source_id = it->second.source_id;
    active_tracks_.erase(it);

    const auto source = recent_sources_.find(source_id);
    if (source != recent_sources_.end()) {
        source->second.last_seen = timestamp;
    }

    pruneRecentSources(timestamp);
}

}  // namespace audition
