#pragma once

#include <cstddef>
#include <vector>

#include <audition/core/export.hpp>

namespace audition {

// Deterministic 2x interpolation for mono 16 kHz sources feeding 32 kHz
// EfficientAT classifiers. No implicit resampling inside IAudioClassifier.
// Boundary samples use symmetric extension. Stateless: entire contiguous
// windows only, never separately resample arbitrary capture hops.
AUDITION_API std::vector<float> efficientAtUpsample16To32(
    const float* samples, std::size_t count);

} // namespace audition
