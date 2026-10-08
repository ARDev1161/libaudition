#pragma once

#include <cstddef>

#include <audition/audio/audio_buffer.hpp>
#include <audition/core/export.hpp>

namespace audition::dsp {

[[nodiscard]] AUDITION_API double rms(AudioView audio) noexcept;
[[nodiscard]] AUDITION_API double channelRms(AudioView audio, std::size_t channel_index);
[[nodiscard]] AUDITION_API double peakAbsolute(AudioView audio) noexcept;
[[nodiscard]] AUDITION_API double clippingRatio(AudioView audio, double threshold = 0.999) noexcept;

/** @brief Converts planar/interleaved layout without changing samples or channel order. */
[[nodiscard]] AUDITION_API AudioBuffer convertLayout(AudioView audio, AudioLayout target_layout);

/** @brief Equal-weight mixdown of all channels to one mono channel. */
[[nodiscard]] AUDITION_API AudioBuffer mixToMono(AudioView audio);

}  // namespace audition::dsp
