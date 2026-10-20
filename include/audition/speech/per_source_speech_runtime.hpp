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
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace audition {

// Source-specific VAD -> speech segmentation -> asynchronous ASR.
// VAD runs synchronously in push() under a mutex; only ASR runs on a worker.
// Strict-deadline capture must use a caller-owned bounded processing queue.
// No ODAS, Qt, ROS or sherpa dependency.
struct PerSourceSpeechOptions {
    std::uint32_t sample_rate_hz{16000};
    // 0: one second of mono PCM at sample_rate_hz; custom limits must
    // be positive and no greater than one second.
    std::size_t max_input_chunk_samples{0};
    std::size_t max_sources{16};
    std::size_t max_pending_segments{16};
    // Caps retired generations awaiting client-side sidecar/result release.
    std::size_t max_retired_generations{64};
    std::size_t min_speech_samples{1600};
    std::size_t pre_roll_samples{3200}; // 200ms @ 16kHz
    std::size_t post_roll_samples{4800}; // 300ms @ 16kHz
    std::size_t max_segment_samples{160000};
    // Bounds retained ASR result text (including token and word strings).
    std::size_t max_result_text_bytes{16384};
    // Bounds the combined number of timed tokens and words per result.
    std::size_t max_result_items{512};
};

struct SourceSpeechResult {
    std::uint64_t track_id{0};
    std::uint64_t generation{0};
    Transcript transcript{};
    Timestamp start_time{};
    Timestamp end_time{};
};

// Atomic consumer snapshot: results and completed retired generations
// are observed under the same mutex.
struct SourceSpeechDrain {
    std::vector<SourceSpeechResult> results{};
    std::vector<std::uint64_t> ready_retired_generations{};
};

struct SourceSpeechTrackStatus {
    bool exists{false};
    bool speech_active{false};
    std::size_t buffered_samples{0};
    std::size_t voiced_samples{0};
    std::optional<std::string> error{};
    // Successful VAD input and ASR queue outcomes for the active generation.
    std::uint64_t received_samples{0};
    std::uint64_t queued_segments{0};
    std::uint64_t dropped_segments{0};
};

struct SourceSpeechStatistics {
    std::uint64_t completed_segments{0};
    std::uint64_t dropped_segments{0};
    std::size_t pending_segments{0};
    std::optional<std::string> last_error{};
    // Number of completed transcripts held until the application drains them.
    // This mailbox is independently capped at max_pending_segments.
    std::size_t buffered_results{0};
    // Completed transcripts evicted from the mailbox due to a slow consumer.
    // Excludes explicit forget/reset cancellations and queued-job overflow.
    std::uint64_t evicted_results{0};
    // Retired source generations still awaiting releaseRetired().
    std::size_t retired_generations{0};
};

class AUDITION_API PerSourceSpeechRuntime final {
public:
    PerSourceSpeechRuntime(const IVoiceActivityDetector& vad,
                           std::shared_ptr<const IAsrEngine> asr,
                           PerSourceSpeechOptions options = {});
    ~PerSourceSpeechRuntime();
    PerSourceSpeechRuntime(const PerSourceSpeechRuntime&) = delete;
    PerSourceSpeechRuntime& operator=(const PerSourceSpeechRuntime&) = delete;

    // Serialized producer calls: VAD processing and state mutation happen
    // synchronously. Never call this from a hard real-time capture callback.
    // Oversized chunks are rejected before VAD and source mutation.
    // Samples are copied only for speech and configured pre/post-roll.
    // A track with an active VAD session is never shared with another track.
    void push(std::uint64_t track_id, AudioView audio);
    // Retire a track and cancel unfinished segments/results; no old text may
    // be delivered under a reused numeric ODAS ID.
    void forget(std::uint64_t track_id);
    // Retire and flush final speech without blocking ASR. Results remain
    // keyed by source generation even if numeric track ID is reused.
    // At max_retired_generations, returns nullopt and leaves an existing
    // active source untouched; caller must drain/release or explicitly forget.
    [[nodiscard]] std::optional<std::uint64_t> retire(std::uint64_t track_id);
    [[nodiscard]] std::size_t pendingResults(std::uint64_t generation) const;
    [[nodiscard]] std::optional<std::uint64_t> activeGeneration(std::uint64_t track_id) const;
    [[nodiscard]] SourceSpeechDrain drainReady();
    // Release finished retirement metadata once results have been consumed.
    void releaseRetired(std::uint64_t generation);
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
        std::uint64_t received_samples{0};
        std::uint64_t queued_segments{0};
        std::uint64_t dropped_segments{0};
    };
    struct Job {
        std::uint64_t track_id{0};
        std::uint64_t generation{0};
        SpeechSegment segment{};
    };
    void queueSegment(std::uint64_t track_id, Track& track);
    void workerLoop();
    void completeJobLocked(std::uint64_t generation);
    const IVoiceActivityDetector& vad_;
    std::shared_ptr<const IAsrEngine> asr_;
    PerSourceSpeechOptions options_{};
    std::map<std::uint64_t, Track> tracks_{};
    // Globally unique generation; avoids retaining unbounded historical IDs.
    std::uint64_t next_generation_{1};
    std::deque<Job> jobs_{};
    std::map<std::uint64_t, std::size_t> pending_by_generation_{};
    std::set<std::uint64_t> retired_generations_{};
    std::vector<SourceSpeechResult> results_{};
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::thread worker_{};
    bool stopping_{false};
    std::uint64_t next_segment_{1};
    std::uint64_t completed_{0};
    std::uint64_t dropped_{0};
    std::uint64_t evicted_results_{0};
    std::optional<std::string> error_{};
};

} // namespace audition
