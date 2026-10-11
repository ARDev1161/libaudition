#include <audition/classify/progressive_source_classification_runtime.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace audition {

ProgressiveSourceClassificationRuntime::ProgressiveSourceClassificationRuntime(
    std::uint32_t sample_rate_hz,
    std::vector<ProgressiveClassificationStage> stages,
    std::size_t max_sources) {
    if (sample_rate_hz == 0U || max_sources == 0U || stages.empty()) {
        throw std::invalid_argument{"Progressive runtime needs stages, sample rate and capacity"};
    }
    // Validate before starting any background threads.
    std::unordered_set<std::string> names;
    for (const auto& stage : stages) {
        if (stage.name.empty() || !names.insert(stage.name).second ||
            !stage.classifier || stage.window_samples == 0U ||
            (stage.hop_samples != 0U && stage.hop_samples > stage.window_samples)) {
            throw std::invalid_argument{"Invalid progressive classifier stage"};
        }
        const auto& supported = stage.classifier->capabilities().audio.supported_sample_rates_hz;
        if (!supported.empty()) {
            bool accepted = false;
            for (const auto rate : supported) {
                if (rate == sample_rate_hz) accepted = true;
            }
            if (!accepted) {
                throw std::invalid_argument{"Progressive stage does not support sample rate"};
            }
        }
    }
    stages_.reserve(stages.size());
    for (auto& stage : stages) {
        Stage owned{};
        owned.name = std::move(stage.name);
        const std::size_t hop = stage.hop_samples == 0U
            ? stage.window_samples : stage.hop_samples;
        owned.max_result_lag_samples = stage.max_result_lag_samples != 0U
            ? static_cast<std::uint64_t>(stage.max_result_lag_samples)
            : static_cast<std::uint64_t>(
                  hop > std::numeric_limits<std::size_t>::max() / 2U
                      ? hop : 2U * hop);
        owned.runtime = std::make_unique<SourceClassificationRuntime>(
            std::move(stage.classifier), sample_rate_hz, stage.window_samples,
            max_sources, stage.hop_samples);
        stages_.push_back(std::move(owned));
    }
}

ProgressiveSourceClassificationRuntime::~ProgressiveSourceClassificationRuntime() = default;

void ProgressiveSourceClassificationRuntime::push(
    std::uint64_t source_id, const float* samples, std::size_t count) {
    for (const auto& stage : stages_) {
        stage.runtime->push(source_id, samples, count);
    }
}

ProgressiveClassificationStatus ProgressiveSourceClassificationRuntime::status(
    std::uint64_t source_id) const {
    ProgressiveClassificationStatus result{};
    result.stages.reserve(stages_.size());
    std::uint64_t newest_audio_end = 0U;
    for (const auto& stage : stages_) {
        auto current = stage.runtime->status(source_id);
        newest_audio_end = std::max(newest_audio_end, current.received_samples);
        result.stages.push_back({stage.name, std::move(current)});
    }
    // A stalled five-second refiner must not conceal fresher ~1s events.
    // Evaluate in ascending priority; only completed predictions from
    // sufficiently recent audio can replace a lower-priority stage.
    for (std::size_t i = 0U; i < result.stages.size(); ++i) {
        const auto& current = result.stages[i].status;
        if (!current.result.has_value() || !current.result_end_sample.has_value()) {
            continue;
        }
        const auto prediction_end = *current.result_end_sample;
        const auto lag = newest_audio_end >= prediction_end
            ? newest_audio_end - prediction_end : 0U;
        if (lag <= stages_[i].max_result_lag_samples) {
            result.selected_stage = i;
            result.result = current.result;
        }
    }
    return result;
}

void ProgressiveSourceClassificationRuntime::forget(std::uint64_t source_id) {
    for (const auto& stage : stages_) {
        stage.runtime->forget(source_id);
    }
}
} // namespace audition
