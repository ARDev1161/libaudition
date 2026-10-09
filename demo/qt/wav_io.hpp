#pragma once

#include <cstddef>
#include <filesystem>

#include <audition/audio/audio_buffer.hpp>

namespace demo {

struct WavFileInfo {
    std::filesystem::path path{};
    std::uint16_t format_tag{0U};
    std::uint16_t bits_per_sample{0U};
};

struct LoadedWav {
    audition::AudioBuffer audio{};
    WavFileInfo info{};
};

[[nodiscard]] LoadedWav loadWav(
    const std::filesystem::path& path);

void saveWavPcm16(
    const std::filesystem::path& path,
    audition::AudioView audio);

[[nodiscard]] audition::AudioBuffer selectMonoChannel(
    audition::AudioView audio,
    std::size_t channel_index);

}  // namespace demo
