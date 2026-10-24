#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace audition::dsp {

// A stateful, replaceable speech gain processor; one instance per source.
class ISourceGain {
public:
    virtual ~ISourceGain() = default;
    virtual std::vector<float> process(const float* pcm, std::size_t count,
                                       std::uint32_t sample_rate) = 0;
    virtual double gainDb() const noexcept = 0;
    virtual void reset() noexcept = 0;
};

// Bounded per-source RMS AGC, applied after source separation only.
class RmsSourceGain final : public ISourceGain {
public:
    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if (!sample_rate || (!pcm && count))
            throw std::invalid_argument("Invalid source gain PCM");
        std::vector<float> result(count);
        if (!count) return result;
        double sum = 0.0, peak = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite source gain sample");
            sum += static_cast<double>(pcm[i]) * pcm[i];
            peak = std::max(peak, std::abs(static_cast<double>(pcm[i])));
        }
        const double rms = std::sqrt(sum / static_cast<double>(count));
        const double rms_db = 20.0 * std::log10(std::max(1e-9, rms));
        constexpr double ceiling = 0.89;
        const double peak_db = peak > 0.0 ?
            20.0 * std::log10(ceiling / peak) : 24.0;
        const double wanted = std::min(
            rms >= 0.001 ? std::clamp(-26.0 - rms_db, 0.0, 24.0) : 0.0,
            peak_db);
        const double tau = wanted < gain_db_ ? 0.02 : 0.075;
        const double alpha = -std::expm1(
            -static_cast<double>(count) / (sample_rate * tau));
        const double initial = std::min(gain_db_, peak_db);
        const double end = std::min(gain_db_ + alpha * (wanted - gain_db_), peak_db);
        for (std::size_t i = 0; i < count; ++i) {
            const double fraction = static_cast<double>(i + 1) / static_cast<double>(count);
            const double gain = std::pow(
                10.0, (initial + (end - initial) * fraction) / 20.0);
            result[i] = static_cast<float>(
                std::clamp(static_cast<double>(pcm[i]) * gain, -ceiling, ceiling));
        }
        gain_db_ = end;
        return result;
    }
    double gainDb() const noexcept override { return gain_db_; }
    void reset() noexcept override { gain_db_ = 0.0; }
private:
    double gain_db_{0.0};
};

// Factory is intentionally replaceable by applications and later algorithms.
inline std::unique_ptr<ISourceGain> makeSourceGain(const char* implementation) {
    if (implementation && std::string(implementation) == "rms")
        return std::make_unique<RmsSourceGain>();
    throw std::invalid_argument("Unknown source gain implementation");
}
} // namespace audition::dsp
