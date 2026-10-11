#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <audition/core/export.hpp>

namespace audition {

// EfficientAT reference preprocessing configuration used by the published
// mn10_as and dymn10_as inference scripts. This is a contract, NOT the
// waveform-to-logmel implementation or an ONNX model adapter.
struct EfficientAtFrontendSpec {
    std::uint32_t sample_rate_hz{32000U};
    std::size_t window_samples{800U};
    std::size_t hop_samples{320U};
    std::size_t fft_size{1024U};
    std::size_t mel_bins{128U};
    std::size_t minimum_audio_samples{1024U};

    [[nodiscard]] bool valid() const noexcept {
        return sample_rate_hz == 32000U &&
               window_samples == 800U &&
               hop_samples == 320U &&
               fft_size == 1024U &&
               mel_bins == 128U;
    }

    // Ensures a standalone 32 kHz waveform is not silently passed as 16 kHz.
    void requireMono32k(std::uint32_t rate, std::uint32_t channels) const {
        if (!valid() || rate != sample_rate_hz || channels != 1U) {
            throw std::invalid_argument{
                "EfficientAT frontend requires mono 32000 Hz PCM and reference configuration"};
        }
    }
};

} // namespace audition
