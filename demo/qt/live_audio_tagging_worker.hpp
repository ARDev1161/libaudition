#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <audition/classify/source_classification_runtime.hpp>
#include <audition/classify/vocabulary.hpp>

// Qt demo is only an adapter: model inference, audio buffering and scheduling
// live in the library (audition::SourceClassificationRuntime).
namespace demo {

struct LiveTaggingOptions {
    std::string model_path{};
    std::string labels_path{};
    bool ced_model{false};
    bool yamnet_model{false};
    std::uint32_t sample_rate_hz{16000U};
    std::uint32_t hop_size{128U};
    audition::VocabularySelection vocabulary{};
};

struct LiveTrackClassification {
    std::string label{};
    double probability{-1.0};
    std::string top_classes{};
    std::vector<std::string> unsupported_labels{};
};

class LiveAudioTaggingWorker final {
public:
    explicit LiveAudioTaggingWorker(LiveTaggingOptions options);
    ~LiveAudioTaggingWorker();
    LiveAudioTaggingWorker(const LiveAudioTaggingWorker&) = delete;
    LiveAudioTaggingWorker& operator=(const LiveAudioTaggingWorker&) = delete;

    void push(std::uint64_t track_id, const std::vector<float>& mono,
              std::uint64_t processed_hops);
    [[nodiscard]] std::optional<LiveTrackClassification> result(
        std::uint64_t track_id, std::uint64_t processed_hops) const;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] std::string status(std::uint64_t track_id) const;
private:
    std::unique_ptr<audition::SourceClassificationRuntime> runtime_{};
    std::string initialization_error_{};
    audition::VocabularySelection selection_{};
};

} // namespace demo
