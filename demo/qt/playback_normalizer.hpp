#pragma once
#include <audition/io/wav_io.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace demo {
struct NormalizedWav {
    std::filesystem::path path;
    double gain_db{0};
};
// Adapter only: DSP, PCM16 validation, streaming WAV I/O live in libaudition.
// Call from QtConcurrent, not the GUI thread.
inline NormalizedWav normalizePlaybackWav(const std::filesystem::path& source) {
    static std::atomic<std::uint64_t> next{0};
    const auto n = next.fetch_add(1, std::memory_order_relaxed);
    const auto clock = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::temp_directory_path() /
        ("libaudition-playback-" + std::to_string(clock) + "-" +
         std::to_string(n) + ".wav");
    const auto normalized = audition::io::normalizePcm16Wav(source, output);
    return {output, normalized.gain_db};
}
} // namespace demo
