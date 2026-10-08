#pragma once

#include <string>

#include <audition/backends/sherpa/options.hpp>

#include <sherpa-onnx/c-api/cxx-api.h>

namespace audition::sherpa_detail {

[[nodiscard]] sherpa_onnx::cxx::SpeakerEmbeddingExtractorConfig
makeSpeakerEmbeddingConfig(const SherpaSpeakerEmbeddingOptions& options);

[[nodiscard]] sherpa_onnx::cxx::OfflineSpeakerDiarizationConfig
makeSpeakerDiarizationConfig(const SherpaSpeakerDiarizationOptions& options);

[[nodiscard]] std::string speakerModelId(const SherpaSpeakerEmbeddingOptions& options);

}  // namespace audition::sherpa_detail
