#include <audition/backends/efficientat/frontend.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <audition/core/error.hpp>

namespace audition {
namespace {
constexpr std::size_t kFft = 1024U;
constexpr std::size_t kWin = 800U;
constexpr std::size_t kHop = 320U;
constexpr std::size_t kMels = 128U;
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kRate = 32000.0;
constexpr double kMaxFreq = 15000.0;

double hzToMel(double hz) { return 1127.0 * std::log1p(hz / 700.0); }

std::array<std::array<float, kFft / 2U + 1U>, kMels> makeMelBanks() {
    std::array<std::array<float, kFft / 2U + 1U>, kMels> banks{};
    const double high = hzToMel(kMaxFreq);
    const double delta = high / static_cast<double>(kMels + 1U);
    for (std::size_t bin = 0; bin < kFft / 2U; ++bin) {
        const double mel = hzToMel(static_cast<double>(bin) * kRate / kFft);
        for (std::size_t m = 0; m < kMels; ++m) {
            const double left = static_cast<double>(m) * delta;
            const double mid = left + delta;
            const double right = mid + delta;
            const double up = (mel - left) / delta;
            const double down = (right - mel) / delta;
            banks[m][bin] = static_cast<float>(std::max(0.0, std::min(up, down)));
        }
    }
    // The 513th FFT bin is zero-padded in EfficientAT's Kaldi mel basis.
    return banks;
}

std::array<float, kFft> makeWindow() {
    std::array<float, kFft> window{};
    const std::size_t start = (kFft - kWin) / 2U;
    for (std::size_t i = 0; i < kWin; ++i) {
        window[start + i] = static_cast<float>(
            0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                 static_cast<double>(kWin - 1U)));
    }
    return window;
}

std::size_t reflected(std::int64_t index, std::size_t len) {
    const auto n = static_cast<std::int64_t>(len);
    while (index < 0 || index >= n) {
        if (index < 0) index = -index;
        if (index >= n) index = 2 * n - 2 - index;
    }
    return static_cast<std::size_t>(index);
}

void fft(std::array<std::complex<double>, kFft>& a) {
    for (std::size_t i = 1, j = 0; i < kFft; ++i) {
        std::size_t bit = kFft >> 1U;
        while (j & bit) {
            j ^= bit;
            bit >>= 1U;
        }
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= kFft; len <<= 1U) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> primitive{std::cos(angle), std::sin(angle)};
        for (std::size_t i = 0; i < kFft; i += len) {
            std::complex<double> w{1.0, 0.0};
            for (std::size_t j = 0; j < len / 2U; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2U] * w;
                a[i + j] = u + v;
                a[i + j + len / 2U] = u - v;
                w *= primitive;
            }
        }
    }
}
} // namespace

EfficientAtLogMel EfficientAtWaveformFrontend::compute(AudioView audio) const {
    if (audio.format().sample_rate_hz != 32000U ||
        audio.format().channel_count != 1U ||
        !audio.format().valid()) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "EfficientAT frontend requires mono float32 32 kHz audio"};
    }
    if (audio.sampleCount() < 515U || audio.sampleCount() > 640000U) {
        throw Error{ErrorCode::InvalidArgument,
                    "EfficientAT input length must be between 515 and 640000 samples"};
    }
    std::vector<float> emphasized(audio.sampleCount() - 1U);
    for (std::size_t i = 0; i < emphasized.size(); ++i) {
        if (!std::isfinite(audio.data()[i]) ||
            !std::isfinite(audio.data()[i + 1U])) {
            throw Error{ErrorCode::InvalidArgument, "Nonfinite EfficientAT input"};
        }
        emphasized[i] = audio.data()[i + 1U] - 0.97F * audio.data()[i];
    }

    const std::size_t frames = emphasized.size() / kHop + 1U;
    EfficientAtLogMel result{};
    result.frames = frames;
    result.values.resize(kMels * frames);

    static const auto banks = makeMelBanks();
    static const auto window = makeWindow();
    std::array<std::complex<double>, kFft> spectrum{};
    std::array<double, kFft / 2U + 1U> power{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto center = static_cast<std::int64_t>(frame * kHop);
        for (std::size_t i = 0; i < kFft; ++i) {
            const auto idx = reflected(center + static_cast<std::int64_t>(i) -
                                       static_cast<std::int64_t>(kFft / 2U),
                                       emphasized.size());
            spectrum[i] = {static_cast<double>(emphasized[idx]) *
                           static_cast<double>(window[i]), 0.0};
        }
        fft(spectrum);
        for (std::size_t b = 0; b < power.size(); ++b) power[b] = std::norm(spectrum[b]);
        for (std::size_t m = 0; m < kMels; ++m) {
            double energy = 0.0;
            for (std::size_t b = 0; b < kFft / 2U; ++b) {
                energy += static_cast<double>(banks[m][b]) * power[b];
            }
            result.values[m * frames + frame] = static_cast<float>(
                (std::log(energy + 1.e-5) + 4.5) / 5.0);
        }
    }
    return result;
}
} // namespace audition
