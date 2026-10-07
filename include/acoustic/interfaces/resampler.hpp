#pragma once

#include <memory>

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/backend/capabilities.hpp>

namespace acoustic {

struct ResamplerConfig {
    std::uint32_t input_sample_rate_hz{0};
    std::uint32_t output_sample_rate_hz{0};
    std::uint32_t channel_count{0};
};

class IAudioResamplerSession {
public:
    virtual ~IAudioResamplerSession() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual AudioBuffer process(AudioView audio) = 0;
    [[nodiscard]] virtual AudioBuffer flush() = 0;
};

class IAudioResampler {
public:
    virtual ~IAudioResampler() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual std::unique_ptr<IAudioResamplerSession> createSession(
        const ResamplerConfig& config) const = 0;
};

}  // namespace acoustic
