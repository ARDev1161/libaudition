#pragma once

#include <cstddef>
#include <vector>

#include <audition/core/geometry.hpp>

namespace audition {

/** Geometry of a single microphone expressed in the array sensor frame. */
struct MicrophoneGeometry {
    Vec3 position_m{};
    Covariance3 position_covariance_m2{};
    Direction3D direction{Direction3D::fromVector({0.0, 0.0, 1.0})};
};

/** Ordered microphone geometry used by spatial-audio backends. */
struct MicrophoneArrayGeometry {
    std::vector<MicrophoneGeometry> microphones{};

    [[nodiscard]] bool empty() const noexcept { return microphones.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return microphones.size(); }
};

}  // namespace audition
