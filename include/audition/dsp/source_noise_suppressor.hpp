#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace audition::dsp {

// One instance per logical speech source. Not thread-safe. Callers own
// scheduling and source lifecycle. process() returns count ordered samples.
// This output-vector API is not suitable for a hard-realtime capture callback.
class ISourceNoiseSuppressor {
public:
    virtual ~ISourceNoiseSuppressor() = default;
    virtual std::vector<float> process(const float* pcm, std::size_t count,
                                       std::uint32_t sample_rate) = 0;
    virtual void reset() noexcept = 0;
};

enum class NoiseSuppressionLevel : std::uint8_t {
    low,
    moderate,
    high,
    very_high
};

// Explicit neutral comparison path. Does not add latency, alter source PCM,
// or require a third-party backend.
class BypassSourceNoiseSuppressor final : public ISourceNoiseSuppressor {
public:
    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if (sample_rate == 0U || (!pcm && count != 0U))
            throw std::invalid_argument("Invalid bypass noise suppressor PCM");
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite bypass noise suppressor PCM");
        }
        if (count == 0U) return {};
        return {pcm, pcm + count};
    }
    void reset() noexcept override {}
};

inline std::unique_ptr<ISourceNoiseSuppressor>
makeBypassSourceNoiseSuppressor() {
    return std::make_unique<BypassSourceNoiseSuppressor>();
}

} // namespace audition::dsp
