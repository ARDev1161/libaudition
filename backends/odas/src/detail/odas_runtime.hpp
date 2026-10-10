#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/backends/odas/odas_options.hpp>
#include <audition/core/geometry.hpp>

namespace audition::odas_detail {

struct OdasSlotResult {
    std::uint64_t track_id{0};
    Vec3 direction{};
    double activity{0.0};
    std::optional<std::vector<float>> separated_audio{};
};

struct OdasPotentialSlot {
    Vec3 direction{};
    double score{0.0};
};

struct OdasRuntimeResult {
    std::vector<OdasSlotResult> active_slots{};
    std::vector<std::uint64_t> live_tracker_ids{};
    std::vector<OdasPotentialSlot> potential_slots{};
};

/** Private stateful composition of the libodas processing modules. */
class OdasRuntime {
public:
    explicit OdasRuntime(const OdasOptions& options);
    ~OdasRuntime();

    OdasRuntime(const OdasRuntime&) = delete;
    OdasRuntime& operator=(const OdasRuntime&) = delete;
    OdasRuntime(OdasRuntime&&) noexcept;
    OdasRuntime& operator=(OdasRuntime&&) noexcept;

    void reset();
    [[nodiscard]] OdasRuntimeResult process(AudioView audio);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition::odas_detail
