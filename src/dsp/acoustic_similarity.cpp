#include <audition/dsp/acoustic_similarity.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

#include <audition/core/error.hpp>

namespace audition::dsp {
namespace {

constexpr std::size_t kWindow = 256;
constexpr std::size_t kHop = 128;
constexpr double kPi = 3.14159265358979323846;
constexpr std::array<unsigned, 9> kFrequencyBins{{2, 4, 7, 11, 18, 28, 44, 72, 100}};

struct Overlap {
    std::size_t first_offset;
    std::size_t second_offset;
    std::size_t count;
};

Overlap overlapForDelay(std::size_t count, int delay) {
    const auto magnitude = static_cast<std::size_t>(std::abs(delay));
    return {delay < 0 ? magnitude : 0,
            delay > 0 ? magnitude : 0,
            count - magnitude};
}

std::optional<double> normalizedCorrelation(
    const float* first, const float* second, const Overlap& overlap) {
    long double sum_x = 0.0L;
    long double sum_y = 0.0L;
    long double xx = 0.0L;
    long double yy = 0.0L;
    long double xy = 0.0L;
    for (std::size_t i = 0; i < overlap.count; ++i) {
        const long double x = first[overlap.first_offset + i];
        const long double y = second[overlap.second_offset + i];
        sum_x += x;
        sum_y += y;
        xx += x * x;
        yy += y * y;
        xy += x * y;
    }
    const long double n = static_cast<long double>(overlap.count);
    const long double vx = xx - sum_x * sum_x / n;
    const long double vy = yy - sum_y * sum_y / n;
    if (vx <= std::max(1e-20L, 1e-12L * xx) ||
        vy <= std::max(1e-20L, 1e-12L * yy)) {
        return std::nullopt;
    }
    const long double covariance = xy - sum_x * sum_y / n;
    return std::clamp(static_cast<double>(covariance / std::sqrt(vx * vy)), -1.0, 1.0);
}

// Selected Hann-windowed STFT bins via Goertzel: no external FFT dependency.
std::array<double, kFrequencyBins.size()> spectralFeatures(
    const float* samples, std::size_t start,
    const std::array<double, kWindow>& window) {
    std::array<double, kFrequencyBins.size()> features{};
    for (std::size_t b = 0; b < kFrequencyBins.size(); ++b) {
        const double coefficient = 2.0 * std::cos(2.0 * kPi * kFrequencyBins[b] / kWindow);
        double previous = 0.0;
        double previous2 = 0.0;
        for (std::size_t i = 0; i < kWindow; ++i) {
            const double now = static_cast<double>(samples[start + i]) * window[i] +
                               coefficient * previous - previous2;
            previous2 = previous;
            previous = now;
        }
        const double power = std::max(
            0.0, previous * previous + previous2 * previous2 -
                     coefficient * previous * previous2);
        features[b] = std::log1p(power);
    }
    return features;
}

std::optional<double> spectralDynamics(
    const float* first, const float* second, const Overlap& overlap,
    std::size_t& spectral_frames) {
    if (overlap.count < 2 * kWindow) {
        return std::nullopt;
    }

    std::array<double, kWindow> window{};
    for (std::size_t i = 0; i < kWindow; ++i) {
        window[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (kWindow - 1)));
    }

    std::array<double, kFrequencyBins.size()> previous_first{};
    std::array<double, kFrequencyBins.size()> previous_second{};
    bool have_previous = false;
    long double dot = 0.0L;
    long double norm_first = 0.0L;
    long double norm_second = 0.0L;

    for (std::size_t start = 0; start + kWindow <= overlap.count; start += kHop) {
        const auto current_first = spectralFeatures(
            first, overlap.first_offset + start, window);
        const auto current_second = spectralFeatures(
            second, overlap.second_offset + start, window);
        ++spectral_frames;
        if (have_previous) {
            for (std::size_t b = 0; b < kFrequencyBins.size(); ++b) {
                const long double dx = current_first[b] - previous_first[b];
                const long double dy = current_second[b] - previous_second[b];
                dot += dx * dy;
                norm_first += dx * dx;
                norm_second += dy * dy;
            }
        }
        previous_first = current_first;
        previous_second = current_second;
        have_previous = true;
    }

    // An unchanged spectral envelope does not establish source identity.
    if (norm_first <= 1e-18L || norm_second <= 1e-18L) {
        return std::nullopt;
    }
    return std::clamp(
        static_cast<double>(dot / std::sqrt(norm_first * norm_second)), -1.0, 1.0);
}

}  // namespace

AcousticSimilarityAnalyzer::AcousticSimilarityAnalyzer(AcousticSimilarityOptions options)
    : options_(options) {
    if (options.max_analysis_samples < 2 * kWindow ||
        options.max_analysis_samples > 65536 ||
        options.max_delay_samples > 2048 ||
        options.max_delay_samples > options.max_analysis_samples / 4 ||
        !std::isfinite(options.minimum_rms) ||
        options.minimum_rms < 0.0 || options.minimum_rms > 1.0) {
        throw Error{ErrorCode::ConfigurationError, "Invalid acoustic similarity options"};
    }
}

AcousticSimilarityResult AcousticSimilarityAnalyzer::compare(
    AudioView first, AudioView second) const {
    if (!first.format().valid() || !second.format().valid() ||
        first.format().channel_count != 1 || second.format().channel_count != 1 ||
        first.format().sample_rate_hz != second.format().sample_rate_hz ||
        first.frameCount() != second.frameCount()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Acoustic similarity requires synchronized same-rate mono windows"};
    }

    AcousticSimilarityResult result{};
    const auto count = std::min(first.frameCount(), options_.max_analysis_samples);
    if (count < 2 * kWindow) {
        return result;
    }

    long double energy_first = 0.0L;
    long double energy_second = 0.0L;
    const auto* a = first.data();
    const auto* b = second.data();
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i])) {
            throw Error{ErrorCode::InvalidArgument,
                        "Acoustic similarity requires finite PCM samples"};
        }
        energy_first += static_cast<long double>(a[i]) * a[i];
        energy_second += static_cast<long double>(b[i]) * b[i];
    }

    const auto floor_energy =
        static_cast<long double>(options_.minimum_rms) * options_.minimum_rms * count;
    if (energy_first <= floor_energy || energy_second <= floor_energy) {
        return result;
    }

    const auto max_delay = static_cast<int>(std::min(
        options_.max_delay_samples, count / 4));
    for (int delay = -max_delay; delay <= max_delay; ++delay) {
        const auto overlap = overlapForDelay(count, delay);
        const auto correlation = normalizedCorrelation(a, b, overlap);
        if (!correlation) {
            continue;
        }
        const double magnitude = std::abs(*correlation);
        const double best = result.waveform_correlation ?
                                std::abs(*result.waveform_correlation) : -1.0;
        if (magnitude > best + 1e-12 ||
            (std::abs(magnitude - best) <= 1e-12 &&
             std::abs(delay) < std::abs(result.delay_samples))) {
            result.waveform_correlation = *correlation;
            result.delay_samples = delay;
            result.compared_samples = overlap.count;
        }
    }
    if (result.waveform_correlation) {
        result.spectral_change_similarity = spectralDynamics(
            a, b, overlapForDelay(count, result.delay_samples),
            result.spectral_frames);
    }
    return result;
}

}  // namespace audition::dsp
