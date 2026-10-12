#pragma once
#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace audition {

// Capture-thread-friendly source-specific VAD -> speech segmentation -> ASR.
// VAD sessions and segment buffers are source-local; ASR runs off capture thread.
// No ODAS, Qt, ROS or sherpa dependency.
struct PerSourceSpeechOptions {
    std::uint32_t sample_rate_hz{16000};
    std::size_t max_sources{16};
    std::size_t max_pending_segments{16};
    std::size_t min_speech_samples{1600};
    std::size_t pre_roll_samples{3200}; // 200ms @ 16kHz
    std::size_t post_roll_samples{4800}; // 300ms @ 16kHz
    std::size_t max_segment_samples{160000};
};

struct SourceSpeechResult {
    std::uint64_t track_id{0};
    std::uint64_t generation{0};
    Transcript transcript{};
    Timestamp start_time{};
    Timestamp end_time{};
};

struct SourceSpeechTrackStatus {
    bool exists{false};
    bool speech_active{false};
    std::size_t buffered_samples{0};
    std::size_t voiced_samples{0};
    std::optional<std::string> error{};
};

struct SourceSpeechStatistics {
    std::uint64_t completed_segments{0};
    std::uint64_t dropped_segments{0};
    std::size_t pending_segments{0};
    std::optional<std::string> last_error{};
};

class AUDITION_API PerSourceSpeechRuntime final {
public:
    PerSourceSpeechRuntime(const IVoiceActivityDetector& vad,
                           std::shared_ptr<const IAsrEngine> asr,
                           PerSourceSpeechOptions options = {});
    ~PerSourceSpeechRuntime();
    PerSourceSpeechRuntime(const PerSourceSpeechRuntime&) = delete;
    PerSourceSpeechRuntime& operator=(const PerSourceSpeechRuntime&) = delete;

    // One capture producer thread. Samples are copied only for speech or
    // configured pre/post-roll; ASR inference runs in the worker.
    // A track with an active VAD session is never shared with another track.
    void push(std::uint64_t track_id, AudioView audio);
    // Retire a track and cancel unfinished segments/results; no old text may
    // be delivered under a reused numeric ODAS ID.
    void forget(std::uint64_t track_id);
    // Invalidate all source generations and queued work, e.g. ALSA xrun.
    void reset();
    // Consume completed ASR results (not polling stale snapshots).
    [[nodiscard]] std::vector<SourceSpeechResult> takeResults();
    [[nodiscard]] SourceSpeechStatistics statistics() const;
    [[nodiscard]] SourceSpeechTrackStatus trackStatus(std::uint64_t track_id) const;

private:
    struct Track {
        std::unique_ptr<IVadSession> vad{};
        std::vector<float> speech{};
        std::deque<float> pre_roll{};
        std::size_t trailing_silence_samples{0U};
        std::size_t voiced_samples{0U};
        bool speech_active{false};
        std::optional<std::string> error{};
        Timestamp start_time{};
        std::uint64_t generation{0};
    };
    struct Job {
        std::uint64_t track_id{0};
        std::uint64_t generation{0};
        SpeechSegment segment{};
    };
    void queueSegment(std::uint64_t track_id, Track& track);
    void workerLoop();
    const IVoiceActivityDetector& vad_;
    std::shared_ptr<const IAsrEngine> asr_;
    PerSourceSpeechOptions options_{};
    std::map<std::uint64_t, Track> tracks_{};
    // Globally unique generation; avoids retaining unbounded historical IDs.
    std::uint64_t next_generation_{1};
    std::deque<Job> jobs_{};
    std::vector<SourceSpeechResult> results_{};
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::thread worker_{};
    bool stopping_{false};
    std::uint64_t next_segment_{1};
    std::uint64_t completed_{0};
    std::uint64_t dropped_{0};
    std::optional<std::string> error_{};
};

} // namespace audition
