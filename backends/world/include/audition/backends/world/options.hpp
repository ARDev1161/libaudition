#pragma once

#include <cstddef>

#include <audition/core/execution.hpp>
#include <audition/core/export.hpp>

namespace audition {

enum class WorldF0Algorithm {
    DioStoneMask,
    Harvest,
};

struct WorldVoiceTraitsOptions {
    WorldF0Algorithm algorithm{WorldF0Algorithm::DioStoneMask};
    ExecutionTarget execution{};
    double frame_period_ms{5.0};
    double f0_floor_hz{50.0};
    double f0_ceil_hz{800.0};
    int dio_speed{1};
    double dio_allowed_range{0.1};
    std::size_t minimum_voiced_frames{3U};
};

struct WorldAcousticAnalysisOptions {
    WorldF0Algorithm algorithm{WorldF0Algorithm::DioStoneMask};
    ExecutionTarget execution{};
    double frame_period_ms{5.0};
    double f0_floor_hz{50.0};
    double f0_ceil_hz{800.0};
    int dio_speed{1};
    double dio_allowed_range{0.1};
    double cheaptrick_q1{-0.15};
    double d4c_threshold{0.85};
};

AUDITION_API void validateWorldVoiceTraitsOptions(
    const WorldVoiceTraitsOptions& options);
AUDITION_API void validateWorldAcousticAnalysisOptions(
    const WorldAcousticAnalysisOptions& options);

}  // namespace audition
