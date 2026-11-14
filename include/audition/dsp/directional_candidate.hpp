#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace audition::dsp {

// Experimental pre-SST candidate, NOT blind separation or an ODAS replacement.
// Four synchronized planar ReSpeaker microphone channels in the documented
// +X,+Y,-X,-Y order. The caller must map PCM channels 1..4 explicitly.
class DirectionalCandidateExtractor {
public:
    static std::vector<float> extract(
        const std::array<const float*, 4>& channels, std::size_t frames,
        double azimuth_radians, unsigned sample_rate,
        double sound_speed_m_s = 343.0) {
        if (!std::isfinite(azimuth_radians) || sample_rate == 0 ||
            !std::isfinite(sound_speed_m_s) || sound_speed_m_s <= 0)
            throw std::invalid_argument("invalid direction or acoustic format");
        for (auto* channel : channels)
            if (!channel && frames)
                throw std::invalid_argument("null microphone channel");
        constexpr std::array<double, 4> x{{.032, 0, -.032, 0}};
        constexpr std::array<double, 4> y{{0, .032, 0, -.032}};
        const double dx = std::cos(azimuth_radians);
        const double dy = std::sin(azimuth_radians);
        std::array<double, 4> delays{};
        for (std::size_t m = 0; m < 4; ++m)
            delays[m] = -(x[m] * dx + y[m] * dy) *
                        static_cast<double>(sample_rate) / sound_speed_m_s;
        std::vector<float> output(frames, 0.0f);
        for (std::size_t i = 0; i < frames; ++i) {
            double sum = 0.0;
            bool valid = true;
            for (std::size_t m = 0; m < 4; ++m) {
                const double at = static_cast<double>(i) + delays[m];
                if (at < 0.0 || at >= static_cast<double>(frames - (frames > 0))) {
                    valid = false;
                    break;
                }
                const auto index = static_cast<std::size_t>(at);
                const double fraction = at - static_cast<double>(index);
                const double a = channels[m][index];
                const double b = channels[m][index + 1];
                if (!std::isfinite(a) || !std::isfinite(b))
                    throw std::invalid_argument("non-finite microphone PCM");
                sum += a + (b - a) * fraction;
            }
            if (valid) output[i] = static_cast<float>(sum / 4.0);
        }
        return output;
    }
};

}  // namespace audition::dsp
