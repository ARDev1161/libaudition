#pragma once

#include <vector>

#include <audition/audio/audio_buffer.hpp>

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

[[nodiscard]] SignalMetrics signalMetrics(AudioView audio, double clipping_threshold = 0.999);
[[nodiscard]] SignalMetrics channelSignalMetrics(AudioView audio, std::size_t channel_index,
                                                 double clipping_threshold = 0.999);
[[nodiscard]] AudioQualityReport analyzeQuality(AudioView audio,
                                                double clipping_threshold = 0.999);

/** Computes RMS-based SNR. The noise floor must be measured/provided explicitly. */
[[nodiscard]] double snrDb(double signal_rms, double noise_rms);

}  // namespace audition::dsp
