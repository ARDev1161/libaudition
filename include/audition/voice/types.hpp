#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <audition/core/probability.hpp>

namespace audition {

struct VoiceTraitScore {
    std::string trait{};
    Probability probability{Probability::zero()};
};

struct VoiceTraits {
    std::optional<Gaussian1D> estimated_age_years{};
    std::vector<VoiceTraitScore> categorical_traits{};
    std::optional<double> pitch_mean_hz{};
    std::optional<double> pitch_stddev_hz{};
    std::optional<double> speaking_rate_syllables_per_second{};
};

struct VoiceAcousticFeatures {
    std::uint32_t sample_rate_hz{0U};
    double frame_period_ms{0.0};
    std::size_t fft_size{0U};
    std::size_t frame_count{0U};
    std::size_t frequency_bin_count{0U};

    std::vector<double> time_axis_seconds{};
    std::vector<double> f0_hz{};
    // 1 = voiced, 0 = unvoiced; one entry per frame.
    std::vector<std::uint8_t> voiced_mask{};

    // Row-major [frame_count, frequency_bin_count].
    std::vector<double> spectral_envelope{};
    std::vector<double> aperiodicity{};
};

struct VoiceState {
    std::vector<VoiceTraitScore> emotions{};
    std::optional<Probability> stress_probability{};
    std::optional<double> arousal{};
    std::optional<double> valence{};
};

}  // namespace audition
