#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace audition {
struct ProgressiveWindowPolicy {
    std::size_t fast_window{0};
    std::size_t fast_hop{0};
    std::size_t refined_window{0};
    std::size_t refined_hop{0};
};

// Model-independent preset; callers validate window shapes against their
// classifier capabilities. No ONNX or GUI dependency.
inline ProgressiveWindowPolicy makeFastRefinedWindowPolicy(
    std::uint32_t sample_rate_hz, std::size_t fast_window,
    std::size_t refined_window, std::size_t fast_hop_override = 0,
    std::size_t refined_hop_override = 0) {
    if (!sample_rate_hz || !fast_window || !refined_window ||
        refined_window <= fast_window)
        throw std::invalid_argument("Invalid quick/refined window sizes");
    ProgressiveWindowPolicy result{};
    result.fast_window = fast_window;
    result.refined_window = refined_window;
    result.fast_hop = fast_hop_override != 0 ? fast_hop_override :
                      std::max<std::size_t>(1U, fast_window / 2U);
    result.refined_hop = refined_hop_override != 0 ? refined_hop_override :
                         static_cast<std::size_t>(sample_rate_hz);
    if (result.fast_hop > fast_window ||
        result.refined_hop > refined_window)
        throw std::invalid_argument("Window hop exceeds analysis window");
    return result;
}
} // namespace audition
