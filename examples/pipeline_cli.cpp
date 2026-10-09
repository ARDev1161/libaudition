#include <audition/audition.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <utility>

namespace {

audition::Timestamp ts(std::int64_t nanoseconds) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, 0U}};
}

class NoopFusion final : public audition::ISpatialFusion {
public:
    audition::BackendInfo backendInfo() const override {
        return {"cli-noop-fusion", "1"};
    }

    std::optional<audition::PositionEstimate> fuse(
        const audition::SpatialFusionInput&) const override {
        return std::nullopt;
    }
};

}  // namespace

int main() {
    try {
        audition::SoundPressureCalibrationProfile profile{};
        profile.profile_id = "pipeline-calibration";
        profile.signal_path_id = "pipeline-mic";
        profile.reference_level_db_spl = {94.0, 0.25};
        profile.measured_level_dbfs = {-20.0, 0.25};
        profile.weighting = audition::SoundLevelWeighting::Z;

        const audition::SoundPressureLevelCalibrator calibrator{
            profile};

        audition::DbfsLevelObservation dbfs{};
        dbfs.timestamp = ts(0);
        dbfs.level_dbfs = -40.0;
        dbfs.variance_db2 = 0.25;
        dbfs.weighting = audition::SoundLevelWeighting::Z;
        dbfs.signal_path_id = "pipeline-mic";

        const auto spl = calibrator.calibrate(dbfs);
        if (!spl.has_value()) {
            return 1;
        }

        audition::SourceLevelPrior priors[] = {
            {
                "alarm",
                {80.0, 4.0},
                1.0,
                1.0,
                audition::SoundLevelWeighting::Z,
            },
        };

        audition::RangeEstimationInput range_input{};
        range_input.sound_level = *spl;
        range_input.source_level_priors = priors;

        audition::SoundLevelRangePriorEstimator range_estimator;
        const auto range = range_estimator.estimate(range_input);
        if (!range.has_value()) {
            return 1;
        }

        audition::SpatialTrack track{};
        track.track_id = audition::SpatialTrackId{11U};
        track.first_seen = ts(0);
        track.last_seen = ts(10);
        track.range = *range;
        audition::SourceFingerprint source_fingerprint{};
        source_fingerprint.model_id = "pipeline-fingerprint-v1";
        source_fingerprint.embedding = {1.0F, 0.0F};
        source_fingerprint.quality = audition::Probability::one();
        track.fingerprint = std::move(source_fingerprint);

        audition::TemporalSpatialTrackSmoother smoother;
        smoother.update(track);

        audition::InMemoryAcousticSourceRegistry registry;
        audition::HeuristicSourceIdentityResolver resolver{registry};
        NoopFusion fusion;
        audition::SpatialIdentityCoordinator identity{
            fusion,
            resolver};

        const auto identity_decision = identity.observe(track);
        if (!track.source_id.has_value()) {
            return 1;
        }

        audition::AcousticEventAssembler events;
        const auto event_id = events.begin(ts(0));
        events.updateFromTrack(event_id, track);

        audition::AcousticEventPatch patch{};
        patch.timestamp = ts(20);
        audition::ClassificationResult classification{};
        classification.classes.push_back(
            {"alarm", audition::Probability::from(0.95)});
        patch.classification = std::move(classification);
        events.update(event_id, patch);

        const auto event = events.finish(event_id, ts(30));

        std::cout << "spl_db=" << spl->level_db_spl.mean << '\n';
        std::cout << "range_m=" << range->distance_m.mean << '\n';
        std::cout << "source_id="
                  << identity_decision.source_id.value() << '\n';
        std::cout << "event_id=" << event.event_id.value() << '\n';
        std::cout << "label="
                  << event.classification->classes.front().label << '\n';
        std::cout << "pipeline=ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
