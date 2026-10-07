#pragma once

#include <cstddef>
#include <cstdint>

namespace acoustic {

enum class AudioLayout : std::uint8_t {
    Interleaved,
    Planar,
};

struct AudioFormat {
    std::uint32_t sample_rate_hz{0};
    std::uint32_t channel_count{0};
    AudioLayout layout{AudioLayout::Interleaved};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return sample_rate_hz > 0 && channel_count > 0;
    }
};

}  // namespace acoustic
