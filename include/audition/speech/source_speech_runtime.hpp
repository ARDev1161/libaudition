#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>

namespace audition {

// Streamed mono audio is segmented by per-track VAD sessions. Completed
// utterances are recognized asynchronously by any IAsrEngine (e.g. Sherpa
// Offline ASR). No ODAS, Qt, ALSA or ROS 2 dependency is required.
struct SourceSpeechOptions {
    std::uint32_t sample_rate_hz{16000U};
    std::size_t vad_frame_samples{0U};  // 0: use VAD preferred_frame_count
    std::size_t pre_roll_ms{200U};
    std::size_t post_roll_ms{300U};
    std::size_t min_speech_ms{150U};
    std::size_t max_segment_ms{10000U};
    std::size_t max_sources{16U};
    std::size_t max_queued_chunks{256U};
    std::size_t max_queued_segments{16U};
    std::size_t max_events{64U};
};

struct SourceTranscriptEvent {
    std::uint64_t source_id{0U}; // ODAS track identity; not a stable SpeakerId
    std::uint64_t generation{0U};
    std::uint64_t audio_start_sample{0U};
    std::uint64_t audio_end_sample{0U};
    Transcript transcript{};
};

struct SourceSpeechStatus {
    bool speech_active{false};
    std::uint64_t received_samples{0U};
    std::uint64_t completed_segments{0U};
    std::uint64_t dropped_chunks{0U};
    std::uint64_t dropped_segments{0U};
    std::optional<Transcript> last_transcript{};
    std::string error{};
};

// push() never performs VAD or ASR inference. VAD segmentation and ASR run on
// dedicated workers with bounded input/ASR queues. Forgetting a source
// invalidates queued and in-flight results of its previous generation.
class AUDITION_API SourceSpeechRuntime final {
public:
    SourceSpeechRuntime(std::shared_ptr<IVoiceActivityDetector> vad,
                        std::shared_ptr<IAsrEngine> asr,
                        SourceSpeechOptions options = {});
    ~SourceSpeechRuntime();
    SourceSpeechRuntime(const SourceSpeechRuntime&) = delete;
    SourceSpeechRuntime& operator=(const SourceSpeechRuntime&) = delete;

    // False means the chunk was rejected due to backpressure or capacity.
    // A rejected source is reset so that speech never bridges missing audio.
    bool push(std::uint64_t source_id, AudioView audio);
    void forget(std::uint64_t source_id);
    [[nodiscard]] SourceSpeechStatus status(std::uint64_t source_id) const;
    [[nodiscard]] std::vector<SourceTranscriptEvent> drainTranscripts();
private:
    struct Chunk {
        std::uint64_t source_id{};
        std::uint64_t generation{};
        Timestamp capture_time{};
        std::vector<float> samples{};
    };
    struct SegmentJob {
        std::uint64_t source_id{};
        std::uint64_t generation{};
        std::uint64_t start_sample{};
        std::uint64_t end_sample{};
        SpeechSegment segment{};
    };
    struct SourceState; // exclusively accessed by VAD worker
    void vadLoop() noexcept;
    void asrLoop() noexcept;
    void handleChunk(const Chunk& chunk,
                     std::unordered_map<std::uint64_t, std::unique_ptr<SourceState>>& states);
    void queueSegment(std::uint64_t source_id, std::uint64_t generation,
                      SourceState& state);
    void invalidateLocked(std::uint64_t source_id, bool drop_status);
    [[nodiscard]] bool currentLocked(std::uint64_t source_id,
                                     std::uint64_t generation) const;
    std::shared_ptr<IVoiceActivityDetector> vad_{};
    std::shared_ptr<IAsrEngine> asr_{};
    SourceSpeechOptions options_{};
    std::size_t vad_frames_{0U};
    mutable std::mutex mutex_{};
    std::condition_variable vad_cv_{};
    std::condition_variable asr_cv_{};
    bool stopping_{false};
    std::thread vad_thread_{};
    std::thread asr_thread_{};
    std::deque<Chunk> pending_chunks_{};
    std::deque<SegmentJob> pending_segments_{};
    std::deque<SourceTranscriptEvent> events_{};
    std::unordered_map<std::uint64_t, std::uint64_t> generations_{};
    std::unordered_map<std::uint64_t, SourceSpeechStatus> statuses_{};
    std::uint64_t next_segment_id_{1U};
};

} // namespace audition
