#pragma once
#include <audition/dsp/source_gain.hpp>
#include <audition/dsp/source_noise_suppressor.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace audition::dsp {

// Source-local speech processing: NS -> AGC, never microphone-array PCM.
// State belongs to the two optional processors, one pair per source.
// Input is borrowed and unmodified. Output count must equal input count.
// Errors propagate: consumers must not silently fall back to raw ASR PCM.
// Vector-returning processing is not appropriate on a hard RT callback.
inline std::vector<float> processSourceSpeechChain(
    const float* pcm, std::size_t count, std::uint32_t sample_rate,
    ISourceNoiseSuppressor* ns, ISourceGain* gain) {
    if (!sample_rate || (!pcm && count))
        throw std::invalid_argument("Invalid source speech PCM");
    for (std::size_t i = 0; i < count; ++i)
        if (!std::isfinite(pcm[i]))
            throw std::invalid_argument("Nonfinite source speech PCM");
    if (!count) return {};

    const auto validate = [count](const std::vector<float>& out) {
        if (out.size() != count)
            throw std::runtime_error("Speech DSP changed PCM sample count");
        for (float sample : out)
            if (!std::isfinite(sample))
                throw std::runtime_error("Speech DSP returned nonfinite PCM");
    };

    std::vector<float> denoised;
    const float* current = pcm;
    if (ns) {
        denoised = ns->process(current, count, sample_rate);
        validate(denoised);
        current = denoised.data();
    }
    if (gain) {
        auto amplified = gain->process(current, count, sample_rate);
        validate(amplified);
        return amplified;
    }
    if (ns) return denoised;
    return {pcm, pcm + count};
}

} // namespace audition::dsp
