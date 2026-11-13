#pragma once

#include <cstddef>
#include <optional>

#include <audition/audio/audio_buffer.hpp>
#include <audition/core/export.hpp>

namespace audition::dsp {

/**
 * @brief Bounded comparison of two already-aligned mono candidate audio streams.
 *
 * The caller supplies audio windows on the same sample clock and at the same
 * sample rate. Results are uncalibrated similarity evidence, NOT a probability,
 * proof of source identity, or a source-separation quality metric.
 */
struct AcousticSimilarityOptions {
    std::size_t max_analysis_samples{8192};
    std::size_t max_delay_samples{128};
    double minimum_rms{1e-5};
};

struct AcousticSimilarityResult {
    // Signed Pearson correlation at the best absolute-correlation lag, [-1, 1].
    // A strong value can indicate shared content, including source leakage.
    std::optional<double> waveform_correlation{};

    // Cosine similarity of short-time log-spectral changes at the aligned lag.
    // Nullopt means insufficient spectral dynamics (e.g. silence/steady tone).
    std::optional<double> spectral_change_similarity{};

    // second[t + delay_samples] is aligned with first[t].
    int delay_samples{0};
    std::size_t compared_samples{0};
    std::size_t spectral_frames{0};
};

class AUDITION_API AcousticSimilarityAnalyzer {
public:
    explicit AcousticSimilarityAnalyzer(AcousticSimilarityOptions options = {});

    /** @brief Stateless; never buffers, tracks, or merges sources.
     *
     * Both inputs must be mono, same sample rate and frame count, and correspond
     * to the same capture interval. Compares only the first max_analysis_samples.
     * Empty, silent, or too-short windows return absent evidence (not zero
     * similarity); invalid formats and non-finite PCM throw audition::Error.
     * Not intended for a hard real-time capture callback.
     */
    [[nodiscard]] AcousticSimilarityResult compare(AudioView first, AudioView second) const;

    [[nodiscard]] const AcousticSimilarityOptions& options() const noexcept { return options_; }

private:
    AcousticSimilarityOptions options_;
};

}  // namespace audition::dsp
