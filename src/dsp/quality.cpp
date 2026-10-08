#include <audition/dsp/quality.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include <audition/core/error.hpp>

namespace audition::dsp {
namespace {

double dbfs(double linear) noexcept {
    if (linear <= 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    return 20.0 * std::log10(linear);
}

template <typename Getter>
SignalMetrics computeMetrics(std::size_t count, Getter sample, double clipping_threshold) {
    SignalMetrics metrics{};
    if (count == 0U) {
        metrics.rms_dbfs = -std::numeric_limits<double>::infinity();
        metrics.peak_dbfs = -std::numeric_limits<double>::infinity();
        return metrics;
    }

    clipping_threshold = std::clamp(clipping_threshold, 0.0, 1.0);
    long double sum = 0.0L;
    long double sum_squares = 0.0L;
    std::size_t clipped = 0U;

    for (std::size_t i = 0; i < count; ++i) {
        const auto value = static_cast<double>(sample(i));
        sum += static_cast<long double>(value);
        sum_squares += static_cast<long double>(value) * static_cast<long double>(value);
        metrics.peak_linear = std::max(metrics.peak_linear, std::abs(value));
        if (std::abs(value) >= clipping_threshold) {
            ++clipped;
        }
    }

    metrics.dc_offset = static_cast<double>(sum / static_cast<long double>(count));
    metrics.rms_linear =
        std::sqrt(static_cast<double>(sum_squares / static_cast<long double>(count)));
    metrics.rms_dbfs = dbfs(metrics.rms_linear);
    metrics.peak_dbfs = dbfs(metrics.peak_linear);
    metrics.clipping_ratio =
        static_cast<double>(clipped) / static_cast<double>(count);
    metrics.crest_factor_db =
        metrics.rms_linear > 0.0 ? dbfs(metrics.peak_linear / metrics.rms_linear) : 0.0;
    return metrics;
}

}  // namespace

SignalMetrics signalMetrics(AudioView audio, double clipping_threshold) {
    return computeMetrics(audio.sampleCount(),
                          [&audio](std::size_t i) { return audio.data()[i]; },
                          clipping_threshold);
}

SignalMetrics channelSignalMetrics(AudioView audio, std::size_t channel_index,
                                   double clipping_threshold) {
    const auto channel = audio.channel(channel_index);
    return computeMetrics(channel.size(),
                          [&channel](std::size_t i) { return channel[i]; },
                          clipping_threshold);
}

AudioQualityReport analyzeQuality(AudioView audio, double clipping_threshold) {
    if (!audio.format().valid()) {
        throw Error{ErrorCode::InvalidArgument,
                    "analyzeQuality requires a valid audio format"};
    }
    AudioQualityReport report{};
    report.aggregate = signalMetrics(audio, clipping_threshold);
    report.channels.reserve(audio.format().channel_count);
    for (std::size_t channel = 0; channel < audio.format().channel_count; ++channel) {
        report.channels.push_back(
            channelSignalMetrics(audio, channel, clipping_threshold));
    }
    return report;
}

double snrDb(double signal_rms, double noise_rms) {
    if (!std::isfinite(signal_rms) || !std::isfinite(noise_rms) ||
        signal_rms < 0.0 || noise_rms <= 0.0) {
        throw Error{ErrorCode::InvalidArgument,
                    "snrDb requires finite signal RMS >= 0 and noise RMS > 0"};
    }
    if (signal_rms == 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    return 20.0 * std::log10(signal_rms / noise_rms);
}

}  // namespace audition::dsp
