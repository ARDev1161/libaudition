#pragma once

#include <cstddef>

#include <acoustic/audio/audio_buffer.hpp>

namespace acoustic::dsp {

[[nodiscard]] double rms(AudioView audio) noexcept;
[[nodiscard]] double channelRms(AudioView audio, std::size_t channel_index);
[[nodiscard]] double peakAbsolute(AudioView audio) noexcept;
[[nodiscard]] double clippingRatio(AudioView audio, double threshold = 0.999) noexcept;

/** @brief Converts planar/interleaved layout without changing samples or channel order. */
[[nodiscard]] AudioBuffer convertLayout(AudioView audio, AudioLayout target_layout);

/** @brief Equal-weight mixdown of all channels to one mono channel. */
[[nodiscard]] AudioBuffer mixToMono(AudioView audio);

}  // namespace acoustic::dsp
