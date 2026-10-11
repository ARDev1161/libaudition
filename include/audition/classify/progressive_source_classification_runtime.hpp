#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <audition/classify/source_classification_runtime.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/classify.hpp>

namespace audition {

// Stage order is priority order: later results refine earlier ones.
// Classifier implementations and window/hop lengths are fully independent.
// A stage owns its model and its bounded SourceClassificationRuntime worker.
struct ProgressiveClassificationStage {
    std::string name{};
    std::unique_ptr<IAudioClassifier> classifier{};
    std::size_t window_samples{0U};
    std::size_t hop_samples{0U}; // 0 = non-overlapping windows.
};

struct ProgressiveClassificationStageStatus {
    std::string name{};
    SourceClassificationStatus status{};
};

struct ProgressiveClassificationStatus {
    std::vector<ProgressiveClassificationStageStatus> stages{};
    // Highest-priority stage with a currently valid result.
    // If one stage fails, healthy stages can continue producing predictions.
    std::optional<std::size_t> selected_stage{};
    std::optional<ClassificationResult> result{};
};

// Progressive prediction for each tracked source; e.g. first ~1s YAMNet,
// then a higher-context 5s YAMNet classification. No YAMNet or Qt dependency.
//
// Each stage may use any classifier, sample window or hop. The caller owns
// its stage selection and may inspect every result independently. The longest
// or last window is NOT automatically more accurate, merely preferred here.
class AUDITION_API ProgressiveSourceClassificationRuntime final {
public:
    ProgressiveSourceClassificationRuntime(
        std::uint32_t sample_rate_hz,
        std::vector<ProgressiveClassificationStage> stages,
        std::size_t max_sources = 16U);
    ~ProgressiveSourceClassificationRuntime();

    ProgressiveSourceClassificationRuntime(
        const ProgressiveSourceClassificationRuntime&) = delete;
    ProgressiveSourceClassificationRuntime& operator=(
        const ProgressiveSourceClassificationRuntime&) = delete;

    // Called by one capture producer; inference runs on bounded worker threads.
    void push(std::uint64_t source_id, const float* samples, std::size_t count);
    [[nodiscard]] ProgressiveClassificationStatus status(
        std::uint64_t source_id) const;
    // Discard all stage generations when a tracker ID vanishes or is reused.
    void forget(std::uint64_t source_id);

private:
    struct Stage {
        std::string name{};
        // Explicitly destroy the worker before its classifier dependencies.
        std::unique_ptr<SourceClassificationRuntime> runtime{};
    };
    std::vector<Stage> stages_{};
};

} // namespace audition
