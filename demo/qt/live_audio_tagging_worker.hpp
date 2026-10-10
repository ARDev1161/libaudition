#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Standalone Qt demo helper. Model inference runs strictly outside ALSA's
// capture/ODAS thread. A single pending job bounds work under overload.
namespace demo {

struct LiveTaggingOptions {
    std::string model_path{};
    std::string labels_path{};
    bool ced_model{false};
    std::uint32_t sample_rate_hz{16000};
    std::uint32_t hop_size{128};
};

struct LiveTrackClassification {
    std::string label{};
    double probability{-1.0};
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

private:
    struct TrackBuffer {
        std::vector<float> samples{};
        std::uint64_t last_hop{0};
    };
    struct Job {
        std::uint64_t track_id{0};
        std::uint64_t hop{0};
        std::vector<float> samples{};
    };
    struct Labeled {
        LiveTrackClassification classification{};
        std::uint64_t hop{0};
    };

    void run(LiveTaggingOptions options) noexcept;

    const std::size_t window_samples_;
    const std::uint64_t stale_hops_;
    // The capture thread is sole owner of buffers_ and only uses bounded
    // vectors. mutex_ protects the job and inference result handoff.
    std::map<std::uint64_t, TrackBuffer> buffers_{};
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::optional<Job> pending_{};
    std::map<std::uint64_t, Labeled> results_{};
    std::string error_{};
    bool stopping_{false};
    std::thread worker_{};
};

}  // namespace demo
