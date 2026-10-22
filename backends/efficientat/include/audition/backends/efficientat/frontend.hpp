#pragma once

#include <cstddef>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/core/export.hpp>

namespace audition {

struct EfficientAtLogMel {
    std::size_t mel_bins{128U};
    std::size_t frames{0U};
    // Row-major [mel_bin, time], matching EfficientAT [1,1,128,T].
    std::vector<float> values{};
};

// Reference-oriented inference frontend: 32kHz preemphasis, reflect-padded
// torch STFT (1024 FFT, 800 symmetric Hann, hop 320), Kaldi 128-bank mel,
// power spectrum, log floor and EfficientAT normalization.
// Model parity must still be checked against the original PyTorch frontend.
class AUDITION_API EfficientAtWaveformFrontend {
public:
    [[nodiscard]] EfficientAtLogMel compute(AudioView audio) const;
};

} // namespace audition
