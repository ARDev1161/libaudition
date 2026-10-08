#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <audition/core/execution.hpp>
#include <audition/core/export.hpp>
#include <audition/model/model_descriptor.hpp>

namespace audition {

struct ClapOnnxAudioOptions {
    std::filesystem::path model{};
    std::optional<ModelDescriptor> model_descriptor{};
    ExecutionTarget execution{};
    std::int32_t intra_op_threads{1};
    std::uint32_t sample_rate_hz{48000U};
    std::size_t embedding_dimension{512U};
    std::string model_id{"laion/clap-htsat-unfused"};
};

struct ClapOnnxTextOptions {
    std::filesystem::path model{};
    std::filesystem::path tokenizer{};
    std::optional<ModelDescriptor> model_descriptor{};
    ExecutionTarget execution{};
    std::int32_t intra_op_threads{1};
    std::size_t sequence_length{77U};
    std::size_t embedding_dimension{512U};
    std::string model_id{"laion/clap-htsat-unfused"};
};

struct ClapOpenVocabularyOptions {
    ClapOnnxAudioOptions audio{};
    ClapOnnxTextOptions text{};
    double similarity_temperature{1.0};
};

AUDITION_API void validateClapOnnxAudioOptions(
    const ClapOnnxAudioOptions& options);
AUDITION_API void validateClapOnnxTextOptions(
    const ClapOnnxTextOptions& options);
AUDITION_API void validateClapOpenVocabularyOptions(
    const ClapOpenVocabularyOptions& options);

}  // namespace audition
