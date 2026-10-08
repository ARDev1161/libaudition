#include "detail/clap_features.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <audition/core/error.hpp>

namespace audition::clap_detail {
namespace {

constexpr std::size_t kSampleRate = 48000U;
constexpr std::size_t kFftSize = 1024U;
constexpr std::size_t kHopLength = 480U;
constexpr std::size_t kMelBins = 64U;
constexpr std::size_t kFftBins = kFftSize / 2U + 1U;
constexpr std::size_t kTargetSamples = 480000U;
constexpr double kMinFrequencyHz = 50.0;
constexpr double kMaxFrequencyHz = 14000.0;
constexpr double kMelFloor = 1.0e-10;
constexpr double kPi = 3.141592653589793238462643383279502884;

double hertzToSlaneyMel(double frequency_hz) {
    constexpr double min_log_hz = 1000.0;
    constexpr double min_log_mel = 15.0;
    const double log_step = 27.0 / std::log(6.4);
    if (frequency_hz < min_log_hz) {
        return 3.0 * frequency_hz / 200.0;
    }
    return min_log_mel +
           std::log(frequency_hz / min_log_hz) * log_step;
}

double slaneyMelToHertz(double mel) {
    constexpr double min_log_hz = 1000.0;
    constexpr double min_log_mel = 15.0;
    const double log_step = std::log(6.4) / 27.0;
    if (mel < min_log_mel) {
        return 200.0 * mel / 3.0;
    }
    return min_log_hz *
           std::exp(log_step * (mel - min_log_mel));
}

std::vector<double> buildMelFilterbank() {
    std::vector<double> filter_freqs(kMelBins + 2U);
    const double mel_min = hertzToSlaneyMel(kMinFrequencyHz);
    const double mel_max = hertzToSlaneyMel(kMaxFrequencyHz);
    for (std::size_t i = 0; i < filter_freqs.size(); ++i) {
        const double fraction =
            static_cast<double>(i) /
            static_cast<double>(filter_freqs.size() - 1U);
        const double mel = mel_min + (mel_max - mel_min) * fraction;
        filter_freqs[i] = slaneyMelToHertz(mel);
    }

    std::vector<double> bank(kMelBins * kFftBins, 0.0);
    for (std::size_t mel = 0; mel < kMelBins; ++mel) {
        const double left = filter_freqs[mel];
        const double center = filter_freqs[mel + 1U];
        const double right = filter_freqs[mel + 2U];
        const double area_scale = 2.0 / (right - left);

        for (std::size_t bin = 0; bin < kFftBins; ++bin) {
            const double frequency =
                static_cast<double>(bin) *
                static_cast<double>(kSampleRate) /
                static_cast<double>(kFftSize);
            const double lower =
                (frequency - left) / (center - left);
            const double upper =
                (right - frequency) / (right - center);
            const double triangle =
                std::max(0.0, std::min(lower, upper));
            bank[mel * kFftBins + bin] =
                triangle * area_scale;
        }
    }
    return bank;
}

std::vector<double> buildPeriodicHann() {
    std::vector<double> window(kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        window[i] =
            0.5 -
            0.5 * std::cos(
                2.0 * kPi * static_cast<double>(i) /
                static_cast<double>(kFftSize));
    }
    return window;
}

void fft(std::vector<std::complex<double>>& values) {
    const std::size_t size = values.size();

    for (std::size_t i = 1U, j = 0U; i < size; ++i) {
        std::size_t bit = size >> 1U;
        for (; (j & bit) != 0U; bit >>= 1U) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(values[i], values[j]);
        }
    }

    for (std::size_t length = 2U;
         length <= size;
         length <<= 1U) {
        const double angle =
            -2.0 * kPi / static_cast<double>(length);
        const std::complex<double> root{
            std::cos(angle), std::sin(angle)};

        for (std::size_t offset = 0U;
             offset < size;
             offset += length) {
            std::complex<double> factor{1.0, 0.0};
            const std::size_t half = length / 2U;
            for (std::size_t i = 0U; i < half; ++i) {
                const auto even = values[offset + i];
                const auto odd =
                    values[offset + i + half] * factor;
                values[offset + i] = even + odd;
                values[offset + i + half] = even - odd;
                factor *= root;
            }
        }
    }
}

void validateSamples(AudioView audio) {
    for (std::size_t i = 0U; i < audio.sampleCount(); ++i) {
        if (!std::isfinite(audio.data()[i])) {
            throw Error{
                ErrorCode::InvalidArgument,
                "CLAP audio input contains a non-finite sample"};
        }
    }
}

std::vector<double> repeatPad(AudioView audio) {
    if (audio.sampleCount() > kTargetSamples) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "CLAP audio embedding accepts at most 10 seconds per call; segment longer audio explicitly"};
    }

    std::vector<double> waveform(kTargetSamples, 0.0);
    const std::size_t input_count = audio.sampleCount();
    if (input_count == 0U) {
        return waveform;
    }

    const std::size_t repeats = kTargetSamples / input_count;
    std::size_t write = 0U;
    for (std::size_t repeat = 0U;
         repeat < repeats;
         ++repeat) {
        for (std::size_t i = 0U;
             i < input_count && write < kTargetSamples;
             ++i) {
            waveform[write++] =
                static_cast<double>(audio.data()[i]);
        }
    }
    return waveform;
}

std::vector<double> reflectPad(
    const std::vector<double>& waveform) {
    constexpr std::size_t pad = kFftSize / 2U;
    std::vector<double> padded(
        waveform.size() + 2U * pad, 0.0);

    std::copy(
        waveform.begin(), waveform.end(),
        padded.begin() + static_cast<std::ptrdiff_t>(pad));

    for (std::size_t i = 0U; i < pad; ++i) {
        padded[pad - 1U - i] =
            waveform[i + 1U];
        padded[pad + waveform.size() + i] =
            waveform[waveform.size() - 2U - i];
    }
    return padded;
}

}  // namespace

ClapFeatureExtractor::ClapFeatureExtractor()
    : window_(buildPeriodicHann()),
      mel_filterbank_(buildMelFilterbank()) {}

std::size_t ClapFeatureExtractor::targetSampleCount() const noexcept {
    return kTargetSamples;
}

ClapFeatureTensor ClapFeatureExtractor::extract(
    AudioView audio) const {
    if (audio.sampleCount() == 0U) {
        throw Error{
            ErrorCode::InvalidArgument,
            "CLAP audio embedding requires non-empty audio"};
    }
    validateSamples(audio);

    const auto waveform = repeatPad(audio);
    const auto padded = reflectPad(waveform);
    const std::size_t frame_count =
        1U + (padded.size() - kFftSize) / kHopLength;

    ClapFeatureTensor result;
    result.frames = frame_count;
    result.mel_bins = kMelBins;
    result.values.resize(
        frame_count * kMelBins, 0.0F);

    std::vector<std::complex<double>> spectrum(
        kFftSize);
    std::vector<double> power(kFftBins, 0.0);

    for (std::size_t frame = 0U;
         frame < frame_count;
         ++frame) {
        const std::size_t start = frame * kHopLength;
        for (std::size_t i = 0U; i < kFftSize; ++i) {
            spectrum[i] = {
                padded[start + i] * window_[i], 0.0};
        }

        fft(spectrum);

        for (std::size_t bin = 0U;
             bin < kFftBins;
             ++bin) {
            power[bin] = std::norm(spectrum[bin]);
        }

        for (std::size_t mel = 0U;
             mel < kMelBins;
             ++mel) {
            double energy = 0.0;
            const std::size_t filter_offset =
                mel * kFftBins;
            for (std::size_t bin = 0U;
                 bin < kFftBins;
                 ++bin) {
                energy +=
                    power[bin] *
                    mel_filterbank_[filter_offset + bin];
            }
            const double db =
                10.0 * std::log10(
                    std::max(kMelFloor, energy));
            result.values[frame * kMelBins + mel] =
                static_cast<float>(db);
        }
    }

    return result;
}

}  // namespace audition::clap_detail
