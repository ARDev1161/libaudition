#include <audition/backends/efficientat/resampler.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace audition {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int radius = 16;
double sinc(double x) {
    if (std::abs(x) < 1.e-12) return 1.0;
    return std::sin(pi * x) / (pi * x);
}
std::size_t reflect(std::ptrdiff_t i, std::size_t n) {
    const auto size = static_cast<std::ptrdiff_t>(n);
    while (i < 0 || i >= size) {
        if (i < 0) i = -i;
        if (i >= size) i = size * 2 - i - 2;
    }
    return static_cast<std::size_t>(i);
}
} // namespace

std::vector<float> efficientAtUpsample16To32(const float* samples, std::size_t count) {
    if (samples == nullptr || count < 2U ||
        count > std::numeric_limits<std::size_t>::max() / 2U ||
        count > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max() / 2)) {
        throw std::invalid_argument{"EfficientAT upsample requires at least 2 finite samples"};
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(samples[i])) {
            throw std::invalid_argument{"Nonfinite input to EfficientAT upsampler"};
        }
    }
    std::vector<float> output(count * 2U);
    for (std::size_t i = 0; i < count; ++i) {
        output[i * 2U] = samples[i];
        const double center = static_cast<double>(i) + 0.5;
        double y = 0.0;
        double sum = 0.0;
        for (int offset = -radius + 1; offset <= radius; ++offset) {
            const auto source_index = static_cast<std::ptrdiff_t>(i) + offset;
            const double x = center - static_cast<double>(source_index);
            const double weight = sinc(x) * sinc(x / static_cast<double>(radius));
            y += weight * static_cast<double>(samples[reflect(source_index, count)]);
            sum += weight;
        }
        output[i * 2U + 1U] = static_cast<float>(y / sum);
    }
    return output;
}
} // namespace audition
