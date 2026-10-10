#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
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
    std::uint64_t dropped_windows{0};
    std::string error{};
    std::optional<ClassificationResult> result{};
};

// Independent of ALSA, ODAS, Qt, ROS and model implementation.
// One inference worker. The capture thread never invokes the model.
// Repeated source IDs are not assumed to be persistent identities.
class AUDITION_API SourceClassificationRuntime final {
public:
    SourceClassificationRuntime(std::unique_ptr<IAudioClassifier> classifier,
                                std::uint32_t sample_rate_hz,
                                std::size_t window_samples,
                                std::size_t max_sources = 16U);
    ~SourceClassificationRuntime();
    SourceClassificationRuntime(const SourceClassificationRuntime&) = delete;
    SourceClassificationRuntime& operator=(const SourceClassificationRuntime&) = delete;

    // Concurrent with status(). push() must be called by one capture producer.
    void push(std::uint64_t source_id, const float* samples, std::size_t count);
    [[nodiscard]] SourceClassificationStatus status(std::uint64_t source_id) const;
    void forget(std::uint64_t source_id);
private:
    struct Buffer {
        std::vector<float> samples{};
        std::optional<ClassificationResult> result{};
    };
    struct Job {
        std::uint64_t source_id{0};
        std::vector<float> samples{};
    };
    void run() noexcept;

    const std::uint32_t sample_rate_hz_;
    const std::size_t window_samples_;
    const std::size_t max_sources_;
    std::unique_ptr<IAudioClassifier> classifier_;
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::map<std::uint64_t, Buffer> buffers_{};
    std::optional<Job> pending_{};
    std::optional<std::uint64_t> active_source_{};
    std::string error_{};
    std::uint64_t dropped_windows_{0};
    bool stopping_{false};
    std::thread worker_{};
};

} // namespace audition
