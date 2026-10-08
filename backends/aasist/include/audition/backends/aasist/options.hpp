#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

#include <audition/core/execution.hpp>
#include <audition/core/export.hpp>
#include <audition/model/model_descriptor.hpp>

namespace audition {

struct AasistPlattCalibration {
    double slope{1.0};
    double intercept{0.0};
};

struct AasistOnnxOptions {
    std::filesystem::path model{};
    std::optional<ModelDescriptor> model_descriptor{};
    ExecutionTarget execution{};
    std::int32_t intra_op_threads{1};
    std::uint32_t sample_rate_hz{16000U};
    std::size_t frame_count{64600U};
    std::optional<AasistPlattCalibration> calibration{};
};

AUDITION_API void validateAasistOnnxOptions(
    const AasistOnnxOptions& options);

}  // namespace audition
