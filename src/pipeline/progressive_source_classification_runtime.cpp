#include <audition/classify/progressive_source_classification_runtime.hpp>

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
    for (std::size_t i = 0U; i < stages_.size(); ++i) {
        auto current = stages_[i].runtime->status(source_id);
        if (current.result.has_value()) {
            result.selected_stage = i;
            result.result = current.result;
        }
        result.stages.push_back({stages_[i].name, std::move(current)});
    }
    return result;
}

void ProgressiveSourceClassificationRuntime::forget(std::uint64_t source_id) {
    for (const auto& stage : stages_) {
        stage.runtime->forget(source_id);
    }
}
} // namespace audition
