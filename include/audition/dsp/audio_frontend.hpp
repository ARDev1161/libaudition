#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <audition/audio/audio_buffer.hpp>

namespace audition::dsp {

struct ChannelCalibration {
    double gain{1.0};
    double dc_offset{0.0};
    double delay_samples{0.0};
};

struct ChannelRoute {
    std::size_t input_channel{0};
    ChannelCalibration calibration{};
};

struct AudioFrontendConfig {
    std::vector<ChannelRoute> routes{};
    AudioLayout output_layout{AudioLayout::Interleaved};
};

/**
 * Explicit stateful microphone frontend.
 *
 * Each output channel selects one input channel and applies:
 *   y[n] = gain * (x[n - delay_samples] - dc_offset)
 *
 * delay_samples must be non-negative. Fractional delays use first-order linear
 * interpolation and keep the necessary history across process() calls.
 */
class AudioFrontend {
public:
    explicit AudioFrontend(AudioFrontendConfig config);

    void reset();
    [[nodiscard]] AudioBuffer process(AudioView input);
    [[nodiscard]] const AudioFrontendConfig& config() const noexcept { return config_; }

private:
    struct RouteState {
        std::vector<float> history{};
    };

    void validateInput(AudioView input) const;
    void ensureInitialized(AudioView input);

    AudioFrontendConfig config_{};
    std::vector<RouteState> state_{};
    std::optional<std::uint32_t> sample_rate_hz_{};
    std::optional<std::uint32_t> input_channel_count_{};
};

}  // namespace audition::dsp
