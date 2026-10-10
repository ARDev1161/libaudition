#pragma once

#include "acoustic_sphere_widget.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
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
};

struct LiveCaptureSnapshot {
    bool running{false};
    std::uint64_t generation{0};
    std::uint64_t processed_hops{0};
    std::uint64_t recoveries{0};
    std::vector<AcousticSceneTrack> tracks{};
    std::vector<AcousticScenePotential> potentials{};
    std::vector<double> channel_rms_dbfs{};
    std::vector<double> channel_peak_dbfs{};
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
    [[nodiscard]] LiveCaptureSnapshot snapshot() const;
    [[nodiscard]] static std::vector<AlsaCaptureDevice> discover();

private:
    void run(LiveCaptureConfig config) noexcept;

    mutable std::mutex mutex_{};
    std::thread thread_{};
    std::atomic<bool> stop_requested_{false};
    LiveCaptureSnapshot latest_{};
};

}  // namespace demo
