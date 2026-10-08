#pragma once

#include <cstddef>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/core/export.hpp>

namespace audition::dsp {

struct SignalMetrics {
    double rms_linear{0.0};
    double rms_dbfs{0.0};
    double peak_linear{0.0};
    double peak_dbfs{0.0};
    double dc_offset{0.0};
    double crest_factor_db{0.0};
    double clipping_ratio{0.0};
};

struct AudioQualityReport {
    SignalMetrics aggregate{};
    std::vector<SignalMetrics> channels{};
};

[[nodiscard]] AUDITION_API SignalMetrics signalMetrics(AudioView audio, double clipping_threshold = 0.999);
[[nodiscard]] AUDITION_API SignalMetrics channelSignalMetrics(AudioView audio, std::size_t channel_index,
                                                 double clipping_threshold = 0.999);
[[nodiscard]] AUDITION_API AudioQualityReport analyzeQuality(AudioView audio,
                                                double clipping_threshold = 0.999);

/** Computes RMS-based SNR. The noise floor must be measured/provided explicitly. */
[[nodiscard]] AUDITION_API double snrDb(double signal_rms, double noise_rms);

}  // namespace audition::dsp
