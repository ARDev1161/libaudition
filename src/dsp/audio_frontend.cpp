#include <audition/dsp/audio_frontend.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include <audition/core/error.hpp>

namespace audition::dsp {
namespace {

void validateConfig(const AudioFrontendConfig& config) {
    if (config.routes.empty()) {
        throw Error{ErrorCode::ConfigurationError,
                    "AudioFrontend requires at least one output route"};
    }
    if (config.routes.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw Error{ErrorCode::ConfigurationError,
                    "AudioFrontend output channel count exceeds supported range"};
    }
    for (const auto& route : config.routes) {
        const auto& c = route.calibration;
        if (!std::isfinite(c.gain) || !std::isfinite(c.dc_offset) ||
            !std::isfinite(c.delay_samples) || c.delay_samples < 0.0) {
            throw Error{ErrorCode::ConfigurationError,
                        "AudioFrontend calibration values must be finite and delay non-negative"};
        }
    }
}

float sampleAt(const StridedSpan<const float>& current,
               const std::vector<float>& history,
               std::int64_t index) {
    if (index >= 0) {
        const auto i = static_cast<std::size_t>(index);
        return i < current.size() ? current[i] : 0.0F;
    }

    const auto history_index =
        static_cast<std::int64_t>(history.size()) + index;
    if (history_index < 0 ||
        history_index >= static_cast<std::int64_t>(history.size())) {
        return 0.0F;
    }
    return history[static_cast<std::size_t>(history_index)];
}

void updateHistory(std::vector<float>& history,
                   const StridedSpan<const float>& current) {
    if (history.empty()) {
        return;
    }

    const auto keep = history.size();
    if (current.size() >= keep) {
        for (std::size_t i = 0; i < keep; ++i) {
            history[i] = current[current.size() - keep + i];
        }
        return;
    }

    const auto shift = current.size();
    std::move(history.begin() + static_cast<std::ptrdiff_t>(shift),
              history.end(), history.begin());
    for (std::size_t i = 0; i < current.size(); ++i) {
        history[keep - current.size() + i] = current[i];
    }
}

}  // namespace

AudioFrontend::AudioFrontend(AudioFrontendConfig config) : config_(std::move(config)) {
    validateConfig(config_);
    state_.resize(config_.routes.size());
    for (std::size_t i = 0; i < config_.routes.size(); ++i) {
        const auto delay = config_.routes[i].calibration.delay_samples;
        const auto history_size =
            static_cast<std::size_t>(std::ceil(delay)) + 1U;
        state_[i].history.assign(history_size, 0.0F);
    }
}

void AudioFrontend::reset() {
    for (auto& route : state_) {
        std::fill(route.history.begin(), route.history.end(), 0.0F);
    }
    sample_rate_hz_.reset();
    input_channel_count_.reset();
}

void AudioFrontend::validateInput(AudioView input) const {
    if (!input.format().valid()) {
        throw Error{ErrorCode::InvalidArgument,
                    "AudioFrontend requires a valid input audio format"};
    }
    for (const auto& route : config_.routes) {
        if (route.input_channel >= input.format().channel_count) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "AudioFrontend route refers to a missing input channel"};
        }
    }
    if (sample_rate_hz_.has_value() &&
        *sample_rate_hz_ != input.format().sample_rate_hz) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "AudioFrontend sample rate changed without reset"};
    }
    if (input_channel_count_.has_value() &&
        *input_channel_count_ != input.format().channel_count) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "AudioFrontend input channel count changed without reset"};
    }
}

void AudioFrontend::ensureInitialized(AudioView input) {
    if (!sample_rate_hz_.has_value()) {
        sample_rate_hz_ = input.format().sample_rate_hz;
        input_channel_count_ = input.format().channel_count;
    }
}

AudioBuffer AudioFrontend::process(AudioView input) {
    validateInput(input);
    ensureInitialized(input);

    const auto frames = input.frameCount();
    const auto output_channels = config_.routes.size();
    std::vector<float> output(frames * output_channels, 0.0F);

    for (std::size_t out_channel = 0; out_channel < output_channels; ++out_channel) {
        const auto& route = config_.routes[out_channel];
        const auto& calibration = route.calibration;
        auto& history = state_[out_channel].history;
        const auto source = input.channel(route.input_channel);

        const auto integer_delay =
            static_cast<std::int64_t>(std::floor(calibration.delay_samples));
        const auto fraction = calibration.delay_samples -
                              static_cast<double>(integer_delay);

        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto frame_index = static_cast<std::int64_t>(frame);
            const auto newer = sampleAt(source, history, frame_index - integer_delay);
            const auto older =
                sampleAt(source, history, frame_index - integer_delay - 1);
            const auto delayed =
                static_cast<double>(newer) * (1.0 - fraction) +
                static_cast<double>(older) * fraction;
            const auto calibrated =
                calibration.gain * (delayed - calibration.dc_offset);

            const auto index = config_.output_layout == AudioLayout::Interleaved
                                   ? frame * output_channels + out_channel
                                   : out_channel * frames + frame;
            output[index] = static_cast<float>(calibrated);
        }

        updateHistory(history, source);
    }

    return AudioBuffer{
        std::move(output),
        AudioFormat{input.format().sample_rate_hz,
                    static_cast<std::uint32_t>(output_channels),
                    config_.output_layout},
        input.captureTime(),
        input.sequenceNumber()};
}

}  // namespace audition::dsp
