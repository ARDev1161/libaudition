#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <audition/core/export.hpp>
#include <audition/interfaces/classify.hpp>

namespace audition {

enum class SourceClassificationState {
    WaitingForAudio, Collecting, Queued, Inferencing, Classified, Error
};

struct SourceClassificationStatus {
    SourceClassificationState state{SourceClassificationState::WaitingForAudio};
    std::size_t collected_samples{0};
    std::size_t required_samples{0};
    // Replaced/rejected windows of this source only (not other tracks).
    std::uint64_t dropped_windows{0};
    std::uint64_t completed_windows{0};
    // Timings for the latest successful inference; absent until first result.
    std::optional<double> last_inference_ms{};
    std::optional<double> last_queue_wait_ms{};
    // Aggregated across all successfully completed windows of this source.
    std::optional<double> mean_inference_ms{};
    std::optional<double> mean_queue_wait_ms{};
    std::optional<double> max_inference_ms{};
    std::optional<double> max_queue_wait_ms{};
    // Per-source PCM position: reflects audio ingested, not wall-clock inference time.
    std::uint64_t received_samples{0};
    // End-exclusive PCM position of result; null when result is absent/expired.
    std::optional<std::uint64_t> result_end_sample{};
    std::string error{};
    std::optional<ClassificationResult> result{};
};

struct CompletedSourceClassification {
    std::uint64_t source_id{0};
    std::uint64_t generation{0};
    std::uint64_t start_sample{0};
    std::uint64_t end_sample{0};
    ClassificationResult result{};
};

// Independent of ALSA, ODAS, Qt, ROS and model implementation.
// One inference worker. The capture thread never invokes the model.
// Repeated source IDs are not assumed to be persistent identities.
class AUDITION_API SourceClassificationRuntime final {
public:
    SourceClassificationRuntime(std::unique_ptr<IAudioClassifier> classifier,
                                std::uint32_t sample_rate_hz,
                                std::size_t window_samples,
                                std::size_t max_sources = 16U,
                                std::size_t hop_samples = 0U);
    ~SourceClassificationRuntime();
    SourceClassificationRuntime(const SourceClassificationRuntime&) = delete;
    SourceClassificationRuntime& operator=(const SourceClassificationRuntime&) = delete;

    // Concurrent with status(). push() must be called by one capture producer.
    void push(std::uint64_t source_id, const float* samples, std::size_t count);
    [[nodiscard]] SourceClassificationStatus status(std::uint64_t source_id) const;
    [[nodiscard]] std::vector<CompletedSourceClassification> takeCompleted();
    void forget(std::uint64_t source_id);
private:
    struct Buffer {
        std::vector<float> samples{};
        std::optional<ClassificationResult> result{};
        std::string error{}; // Source-local bad audio must not stop other ODAS tracks.
        std::uint64_t generation{0};
        std::uint64_t last_seen_order{0};
        std::uint64_t received_samples{0};
        std::optional<std::uint64_t> result_end_sample{};
        std::uint64_t dropped_windows{0};
        std::uint64_t completed_windows{0};
        std::optional<double> last_inference_ms{};
        std::optional<double> last_queue_wait_ms{};
        double total_inference_ms{0.0};
        double total_queue_wait_ms{0.0};
        double max_inference_ms{0.0};
        double max_queue_wait_ms{0.0};
        std::chrono::steady_clock::time_point last_result_at{};
    };
    struct Job {
        std::uint64_t source_id{0};
        std::uint64_t generation{0};
        std::uint64_t end_sample{0};
        std::chrono::steady_clock::time_point enqueued_at{};
        std::vector<float> samples{};
    };
    void run() noexcept;

    const std::uint32_t sample_rate_hz_;
    const std::size_t window_samples_;
    const std::size_t max_sources_;
    const std::size_t hop_samples_;
    std::unique_ptr<IAudioClassifier> classifier_;
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::map<std::uint64_t, Buffer> buffers_{};
    std::deque<Job> pending_{};
    std::deque<CompletedSourceClassification> completed_{};
    std::optional<std::uint64_t> active_source_{};
    std::uint64_t active_generation_{0};
    std::uint64_t next_generation_{1};
    std::uint64_t next_seen_order_{1};
    static constexpr auto kResultTtl = std::chrono::seconds{3};
    bool stopping_{false};
    std::thread worker_{};
};

} // namespace audition
