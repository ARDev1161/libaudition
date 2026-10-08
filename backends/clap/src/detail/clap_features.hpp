#pragma once

#include <cstddef>
#include <vector>

#include <audition/audio/audio_buffer.hpp>

namespace audition::clap_detail {

struct ClapFeatureTensor {
    std::vector<float> values{};
    std::size_t frames{0U};
    std::size_t mel_bins{0U};
};

class ClapFeatureExtractor {
public:
    ClapFeatureExtractor();

    [[nodiscard]] ClapFeatureTensor extract(AudioView audio) const;
    [[nodiscard]] std::size_t targetSampleCount() const noexcept;

private:
    std::vector<double> window_{};
    std::vector<double> mel_filterbank_{};
};

}  // namespace audition::clap_detail
