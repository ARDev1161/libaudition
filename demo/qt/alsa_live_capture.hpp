#pragma once

#include "acoustic_sphere_widget.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

// Host audio capture belongs to the Qt demo, NOT the libaudition core.
// When ALSA/ODAS are unavailable, discovery returns no devices and start
// fails explicitly; the existing offline sphere/replay still works.
namespace demo {

struct AlsaCaptureDevice {
    std::string pcm_name{};
    std::string description{};
    bool likely_respeaker{false};
};

struct LiveCaptureConfig {
    std::string pcm_name{};
    std::uint32_t sample_rate_hz{16000};
    std::uint32_t channel_count{6};
    std::uint32_t hop_size{128};
    std::uint32_t frame_size{256};
    std::vector<std::size_t> input_channels{1, 2, 3, 4};
    std::vector<std::array<double, 3>> microphone_positions{};
    bool respeaker_angular_profile{false};
    bool near_plane_sources{false};
    bool tune_sst{true};
    double sst_onset{0.34};
    double sst_release{0.25};
    std::uint32_t sst_inactive_hops{125};
    bool classify_sources{false};
    bool tagging_ced_model{false};
    bool tagging_yamnet_model{false};
    std::string tagging_model_path{};
    std::string tagging_labels_path{};
    bool tagging_selected_vocabulary{false};
    std::vector<std::string> tagging_vocabulary_labels{};
    bool tagging_efficientat_model{false};
    bool transcribe_sources{false};
    bool speech_agc_enabled{false};
    std::string speech_agc_implementation{"rms"};
    std::string speech_vad_kind{"silero"};
    std::string speech_asr_kind{"sensevoice"};
    std::string speech_vad_model_path{};
    std::string speech_asr_model_path{};
    std::string speech_asr_decoder_path{};
    std::string speech_asr_joiner_path{};
    std::string speech_asr_tokens_path{};
    std::string speech_asr_language{"auto"};
};

struct LiveCaptureSnapshot {
    bool running{false};
    std::uint64_t generation{0};
    std::uint64_t processed_hops{0};
    std::uint64_t recoveries{0};
    std::vector<AcousticSceneTrack> tracks{};
    std::map<std::uint64_t, std::string> asr_logs{};
    struct VadState { bool active{false}; std::uint64_t starts{0}; std::uint64_t ends{0}; };
    std::map<std::uint64_t, VadState> vad_states{};
    struct RecordedTrack {
        std::uint64_t id{0};
        std::uint64_t track_id{0};
        std::string path;
        std::uint64_t samples{0};
        bool active{false};
        std::uint64_t lost_samples{0};
        std::string error{};
    };
    std::vector<RecordedTrack> recordings{};
    std::vector<AcousticScenePotential> potentials{};
    std::vector<double> channel_rms_dbfs{};
    std::vector<double> channel_peak_dbfs{};
    struct SourceLevel { std::uint64_t track_id{0}; double rms_dbfs{0}; double peak_dbfs{0}; std::uint64_t samples{0}; };
    std::vector<SourceLevel> sss_levels{};
    struct GainLevel { std::uint64_t track_id{0}; double gain_db{0.0}; };
    std::vector<GainLevel> speech_gain_levels{};
    std::string classification_error{};
    std::string classifier_status{};
    std::string speech_status{};
    std::string error{};
};

// On Linux uses ALSA S16_LE interleaved in a dedicated std::thread and
// feeds exactly one configured hop to an instance-confined ODAS engine.
// The mutex protects a SINGLE latest snapshot: slow UI paints can never
// make capture accumulate an unbounded queue of frames.
class AlsaLiveCapture final {
public:
    AlsaLiveCapture() = default;
    ~AlsaLiveCapture();
    AlsaLiveCapture(const AlsaLiveCapture&) = delete;
    AlsaLiveCapture& operator=(const AlsaLiveCapture&) = delete;

    void start(LiveCaptureConfig config);
    void stop();
    void setSstTuning(double onset, double release, std::uint32_t timeout_hops);
    void setSpeechGain(bool enabled, std::string implementation);
    [[nodiscard]] LiveCaptureSnapshot snapshot() const;
    [[nodiscard]] static std::vector<AlsaCaptureDevice> discover();

private:
    void run(LiveCaptureConfig config) noexcept;

    mutable std::mutex mutex_{};
    std::thread thread_{};
    std::atomic<bool> stop_requested_{false};
    LiveCaptureSnapshot latest_{};
    struct SstTuningRequest {
        double onset{0.34};
        double release{0.25};
        std::uint32_t timeout_hops{125};
        std::uint64_t revision{0};
    };
    SstTuningRequest requested_sst_{};
    bool speech_gain_enabled_{false};
    std::string speech_gain_implementation_{"rms"};
    std::uint64_t speech_gain_revision_{0};
};

}  // namespace demo
