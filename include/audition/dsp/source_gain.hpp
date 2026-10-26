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

// Speech-oriented AGC on source-separated PCM. An energy gate prevents
// amplifying stationary noise; a short hold retains gain between syllables.
// The peak limiter is deliberately independent of the slow RMS controller.
class RmsSourceGain final : public ISourceGain {
public:
    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if (!sample_rate || (!pcm && count))
            throw std::invalid_argument("Invalid source gain PCM");
        std::vector<float> result(count);
        if (!count) return result;
        double energy = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite source gain sample");
            energy += static_cast<double>(pcm[i]) * pcm[i];
        }
        const double seconds = static_cast<double>(count) / sample_rate;
        const double rms = std::sqrt(energy / static_cast<double>(count));
        // Smooth power rather than dB values to avoid discontinuities.
        const double levelAlpha = -std::expm1(-seconds / 0.30);
        if (!initialized_) {
            smoothedPower_ = rms * rms;
            initialized_ = true;
        } else {
            smoothedPower_ += levelAlpha * (rms * rms - smoothedPower_);
        }
        const double smoothedDb = 10.0 * std::log10(std::max(1e-18, smoothedPower_));
        constexpr double kGateDb = -60.0;
        constexpr double kTargetDb = -26.0;
        constexpr double kMaxGainDb = 36.0;
        constexpr double kCeiling = 0.89;
        if (rms >= 0.001) holdSeconds_ = 0.35;
        else holdSeconds_ = std::max(0.0, holdSeconds_ - seconds);
        // On silence do not chase the decaying smoothed RMS; release only
        // after the hold expires to avoid pumping between spoken words.
        const double wanted = holdSeconds_ > 0.0 && smoothedDb > kGateDb
            ? std::clamp(kTargetDb - smoothedDb, 0.0, kMaxGainDb)
            : 0.0;
        const double tau = wanted > gain_db_ ? 0.25 : 0.60;
        const double alpha = -std::expm1(-seconds / tau);
        const double startGain = gain_db_;
        const double endGain = std::clamp(
            gain_db_ + alpha * (wanted - gain_db_), 0.0, kMaxGainDb);
        // A fast peak limiter prevents clipping without forcing the slow AGC
        // envelope to zero after a single transient.
        for (std::size_t i = 0; i < count; ++i) {
            const double fraction = static_cast<double>(i + 1) /
                static_cast<double>(count);
            const double db = startGain + (endGain - startGain) * fraction;
            const double candidate = static_cast<double>(pcm[i]) *
                std::pow(10.0, db / 20.0);
            result[i] = static_cast<float>(
                std::clamp(candidate, -kCeiling, kCeiling));
        }
        gain_db_ = endGain;
        return result;
    }
    double gainDb() const noexcept override { return gain_db_; }
    void reset() noexcept override {
        gain_db_ = 0.0;
        smoothedPower_ = 0.0;
        holdSeconds_ = 0.0;
        initialized_ = false;
    }
private:
    double gain_db_{0.0};
    double smoothedPower_{0.0};
    double holdSeconds_{0.0};
    bool initialized_{false};
};

// Factory is intentionally replaceable by applications and later algorithms.
inline std::unique_ptr<ISourceGain> makeSourceGain(const char* implementation) {
    if (implementation && std::string(implementation) == "rms")
        return std::make_unique<RmsSourceGain>();
    throw std::invalid_argument("Unknown source gain implementation");
}
} // namespace audition::dsp
