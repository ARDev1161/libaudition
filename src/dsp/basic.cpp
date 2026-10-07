#include <acoustic/dsp/basic.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <acoustic/core/error.hpp>

namespace acoustic::dsp {

double rms(AudioView audio) noexcept {
    if (audio.sampleCount() == 0U) {
        return 0.0;
    }
    long double sum = 0.0L;
    for (std::size_t i = 0; i < audio.sampleCount(); ++i) {
        const long double value = static_cast<long double>(audio.data()[i]);
        sum += value * value;
    }
    return std::sqrt(static_cast<double>(sum / static_cast<long double>(audio.sampleCount())));
}

double channelRms(AudioView audio, std::size_t channel_index) {
    const auto channel = audio.channel(channel_index);
    if (channel.empty()) {
        return 0.0;
    }
    long double sum = 0.0L;
    for (std::size_t i = 0; i < channel.size(); ++i) {
        const long double value = static_cast<long double>(channel[i]);
        sum += value * value;
    }
    return std::sqrt(static_cast<double>(sum / static_cast<long double>(channel.size())));
}

double peakAbsolute(AudioView audio) noexcept {
    double peak = 0.0;
    for (std::size_t i = 0; i < audio.sampleCount(); ++i) {
        peak = std::max(peak, std::abs(static_cast<double>(audio.data()[i])));
    }
    return peak;
}

double clippingRatio(AudioView audio, double threshold) noexcept {
    if (audio.sampleCount() == 0U) {
        return 0.0;
    }
    threshold = std::clamp(threshold, 0.0, 1.0);
    std::size_t clipped = 0U;
    for (std::size_t i = 0; i < audio.sampleCount(); ++i) {
        if (std::abs(static_cast<double>(audio.data()[i])) >= threshold) {
            ++clipped;
        }
    }
    return static_cast<double>(clipped) / static_cast<double>(audio.sampleCount());
}

AudioBuffer convertLayout(AudioView audio, AudioLayout target_layout) {
    const auto format = audio.format();
    if (format.layout == target_layout) {
        std::vector<float> copy(audio.data(), audio.data() + audio.sampleCount());
        return AudioBuffer{std::move(copy), format, audio.captureTime(), audio.sequenceNumber()};
    }

    AudioFormat target_format = format;
    target_format.layout = target_layout;
    std::vector<float> output(audio.sampleCount());
    const auto frames = audio.frameCount();

    for (std::size_t channel_index = 0; channel_index < format.channel_count; ++channel_index) {
        const auto channel = audio.channel(channel_index);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (target_layout == AudioLayout::Planar) {
                output[channel_index * frames + frame] = channel[frame];
            } else {
                output[frame * format.channel_count + channel_index] = channel[frame];
            }
        }
    }
    return AudioBuffer{std::move(output), target_format, audio.captureTime(), audio.sequenceNumber()};
}

AudioBuffer mixToMono(AudioView audio) {
    if (!audio.format().valid()) {
        throw Error{ErrorCode::InvalidArgument, "mixToMono requires a valid audio format"};
    }
    const auto frames = audio.frameCount();
    std::vector<float> output(frames, 0.0F);
    const float scale = 1.0F / static_cast<float>(audio.format().channel_count);

    for (std::size_t channel_index = 0; channel_index < audio.format().channel_count; ++channel_index) {
        const auto channel = audio.channel(channel_index);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            output[frame] += channel[frame] * scale;
        }
    }

    return AudioBuffer{std::move(output),
                       AudioFormat{audio.format().sample_rate_hz, 1, AudioLayout::Interleaved},
                       audio.captureTime(), audio.sequenceNumber()};
}

}  // namespace acoustic::dsp
