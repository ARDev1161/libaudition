#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <audition/classify/types.hpp>
#include <audition/core/export.hpp>
#include <audition/core/id.hpp>
#include <audition/core/time.hpp>

namespace audition {

/** A transition in a label's acoustic activity for one persistent source. */
enum class TemporalAcousticEventKind {
    Started,
    Ended,
};

struct TemporalAcousticEvent {
    TemporalAcousticEventKind kind{TemporalAcousticEventKind::Started};
    AcousticSourceId source_id{};
    std::string label{};
    // Locally monotonic occurrence ID, distinct across repeat events and reset().
    std::uint64_t occurrence_id{0};
    // First qualifying classification observation, not measured sound onset.
    Timestamp start_time{};
    // Latest observation supporting activity; never a fabricated sound endpoint.
    Timestamp last_supported_time{};
    // Maximum observed input score during this occurrence, not a calibrated
    // probability that the acoustic event exists.
    Probability peak_score{Probability::zero()};
};

struct TemporalAcousticEventDetectorOptions {
    Probability start_threshold{Probability::from(0.70)};
    Probability stop_threshold{Probability::from(0.40)};
    std::size_t min_start_observations{2U};
    std::size_t min_stop_observations{2U};
    std::size_t max_sources{16U};
    std::size_t max_labels_per_source{64U};
    // Zero disables gap-based closure. A gap larger than this value ends
    // previously active occurrences at their last supported observation.
    Duration max_observation_gap{Duration{0}};
};

/**
 * @brief Synchronous, backend-agnostic temporal classifier event detector.
 *
 * Call observe() with classification *observation times*, not inference
 * completion wall time. Times must increase strictly for each source and keep
 * a stable ClockIdentity. Missing class labels mean score zero.
 *
 * Hysteresis starts an event after consecutive >= start_threshold observations
 * and ends it after consecutive < stop_threshold observations. Brief dips are
 * merged into the active event. Its start is retrospectively dated to the
 * first qualifying observation, its end to the last supporting observation.
 *
 * No scheduling, event storage, cross-modal fusion, or hidden classifier model.
 * Caller may map Started/Ended transitions to AcousticEventAssembler; the
 * detector itself never assumes an AcousticEvent boundary or policy.
 *
 * A source/class state is bounded by explicit configured limits. Capacity,
 * malformed input and clock errors fail without mutating internal state.
 * Use retire() for explicit termination and forget()/reset() for discarding
 * state after discontinuity.
 *
 * One caller/executor owns this instance; it is not internally synchronized.
 */
class AUDITION_API TemporalAcousticEventDetector final {
public:
    explicit TemporalAcousticEventDetector(
        TemporalAcousticEventDetectorOptions options = {});

    [[nodiscard]] std::vector<TemporalAcousticEvent> observe(
        AcousticSourceId source_id,
        Timestamp observation_time,
        const ClassificationResult& classification);

    /** End active occurrences at their last supporting observation. */
    [[nodiscard]] std::vector<TemporalAcousticEvent> retire(
        AcousticSourceId source_id, Timestamp time);

    /** Discard unfinished and active occurrences without notifications. */
    void forget(AcousticSourceId source_id) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::vector<TemporalAcousticEvent> activeEvents() const;
    [[nodiscard]] std::size_t sourceCount() const noexcept;
    [[nodiscard]] const TemporalAcousticEventDetectorOptions&
    options() const noexcept { return options_; }

private:
    struct LabelState {
        bool active{false};
        std::size_t high_count{0U};
        std::size_t low_count{0U};
        std::uint64_t occurrence_id{0U};
        Timestamp candidate_start{};
        Timestamp start_time{};
        Timestamp last_supported{};
        Probability peak{Probability::zero()};
    };

    struct SourceState {
        std::optional<Timestamp> last_observation{};
        std::map<std::string, LabelState> labels{};
    };

    TemporalAcousticEventDetectorOptions options_{};
    std::map<AcousticSourceId, SourceState> sources_{};
    std::uint64_t next_occurrence_id_{1U};
};

}  // namespace audition
