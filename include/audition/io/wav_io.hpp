#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <audition/core/export.hpp>

#include <audition/audio/audio_buffer.hpp>
#include <audition/audio/peak_normalizer.hpp>

namespace audition::io {

struct WavFileInfo {
    std::filesystem::path path{};
    std::uint16_t format_tag{0U};
    std::uint16_t bits_per_sample{0U};
};

struct LoadedWav {
    audition::AudioBuffer audio{};
    WavFileInfo info{};
};

[[nodiscard]] AUDITION_API LoadedWav loadWav(
    const std::filesystem::path& path);

AUDITION_API void saveWavPcm16(
    const std::filesystem::path& path,
    audition::AudioView audio);

[[nodiscard]] AUDITION_API audition::AudioBuffer selectMonoChannel(
    audition::AudioView audio,
    std::size_t channel_index);

struct WavNormalizeResult {
    double gain_db{0.0};
    std::uint64_t samples_written{0};
};
// Two-pass bounded-memory normalization of a mono PCM16 WAV. Does not
// modify the input, suitable for background playback preparation.
AUDITION_API WavNormalizeResult normalizePcm16Wav(
    const std::filesystem::path& input, const std::filesystem::path& output,
    PeakNormalizeOptions options = {});

// Incremental RIFF/PCM16 writer with bounded per-call memory. Caller owns
// scheduling, queue backpressure, and recording-generation identity.
class AUDITION_API StreamingPcm16WavWriter final {
public:
    StreamingPcm16WavWriter(const std::filesystem::path& path,
                            std::uint32_t rate, std::uint16_t channels = 1);
    ~StreamingPcm16WavWriter() noexcept;
    StreamingPcm16WavWriter(const StreamingPcm16WavWriter&) = delete;
    StreamingPcm16WavWriter& operator=(const StreamingPcm16WavWriter&) = delete;
    void append(const std::int16_t* pcm, std::size_t sample_count);
    void appendSilence(std::uint64_t sample_count);
    void flushHeader();
    void finish();
    [[nodiscard]] std::uint64_t sampleCount() const noexcept { return samples_; }
private:
    void writeHeader();
    std::ofstream stream_;
    std::uint32_t rate_{0};
    std::uint16_t channels_{0};
    std::uint64_t samples_{0};
    bool finished_{false};
};

}  // namespace audition::io
