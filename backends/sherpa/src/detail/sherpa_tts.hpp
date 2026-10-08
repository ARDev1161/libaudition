#pragma once

#if defined(LIBAUDITION_SHERPA_TTS_ENABLED)

#include <audition/backends/sherpa/options.hpp>

#include <sherpa-onnx/c-api/cxx-api.h>

namespace audition::sherpa_detail {

[[nodiscard]] sherpa_onnx::cxx::OfflineTtsConfig makeTtsConfig(
    const SherpaTtsOptions& options);

}  // namespace audition::sherpa_detail

#endif
