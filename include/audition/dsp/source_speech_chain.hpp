#pragma once
#include <audition/dsp/source_gain.hpp>
#include <audition/dsp/source_noise_suppressor.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
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
    // Keep the empty and invalid-pointer cases above explicit. In optimized
    // GCC builds a pointer-range vector constructor can be inlined into a
    // memcpy with a spurious -Wnonnull when testing the rejected null input.
    // An indexed copy preserves the validated count without constructing
    // a potentially null pointer range.
    std::vector<float> passthrough(count);
    for (std::size_t i = 0; i < count; ++i)
        passthrough[i] = pcm[i];
    return passthrough;
}

 // Explicit fail-closed policy for non-realtime speech ingress. A failed
 // DSP hop is replaced with an equal-length mute, NOT raw source samples.
 // The caller should mark the matching recording interval as an invalid
 // preprocessing gap and retire/reset any stateful processor that failed.
struct SourceSpeechHopResult {
    std::vector<float> samples{};
    bool failed{false};
    std::string error{};
};

[[nodiscard]] inline SourceSpeechHopResult processSourceSpeechHopOrMute(
    const float* pcm, std::size_t count, std::uint32_t sample_rate,
    ISourceNoiseSuppressor* ns, ISourceGain* gain) {
    try {
        return {processSourceSpeechChain(pcm, count, sample_rate, ns, gain),
                false, {}};
    } catch (const std::exception& e) {
        return {std::vector<float>(count, 0.0F), true, e.what()};
    }
}

} // namespace audition::dsp
