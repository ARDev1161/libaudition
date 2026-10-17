#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace audition::dsp {

struct StreamingLevelSnapshot {
    double rms_dbfs{-180.0};
    double peak_dbfs{-180.0};
    std::uint64_t sample_count{0};
};

// Bounded-state RMS/peak accumulator for periodically published live metrics.
// No Qt, no allocations, no model or capture-backend dependency.
class StreamingLevelMeter final {
public:
    void push(float sample) noexcept {
        const auto v = static_cast<double>(sample);
        if (!std::isfinite(v)) return;
        sum_squares_ += static_cast<long double>(v) * v;
        peak_ = std::max(peak_, std::abs(v));
        ++count_;
    }
    void push(const float* samples, std::size_t count) noexcept {
        if (!samples) return;
        for (std::size_t i = 0; i < count; ++i) push(samples[i]);
    }
    StreamingLevelSnapshot snapshotAndReset() noexcept {
        StreamingLevelSnapshot out;
        out.sample_count = count_;
        if (count_ != 0) {
            const auto rms = std::sqrt(static_cast<double>(
                sum_squares_ / static_cast<long double>(count_)));
            out.rms_dbfs = 20.0 * std::log10(std::max(1.0e-9, rms));
            out.peak_dbfs = 20.0 * std::log10(std::max(1.0e-9, peak_));
        }
        reset();
        return out;
    }
    void reset() noexcept {
        sum_squares_ = 0.0L;
        peak_ = 0.0;
        count_ = 0;
    }
private:
    long double sum_squares_{0};
    double peak_{0};
    std::uint64_t count_{0};
};

} // namespace audition::dsp
