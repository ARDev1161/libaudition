#include "detail/clap_text_encoder.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

#include <onnxruntime_cxx_api.h>

#include "detail/roberta_tokenizer.hpp"

namespace audition::clap_detail {
namespace {

constexpr std::size_t kSequenceLength = 77U;
constexpr std::size_t kEmbeddingDimension = 512U;

void requireConfiguration(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

bool cpuProviderSelected(const ExecutionTarget& execution) {
    return execution.provider.empty() ||
           execution.provider == "cpu" ||
           execution.provider == "CPUExecutionProvider";
}

void validateExecution(const ExecutionTarget& execution) {
    requireConfiguration(
        execution.device_class == DeviceClass::Auto ||
            execution.device_class == DeviceClass::Cpu,
        "CLAP ONNX text backend currently supports CPU execution only");
    requireConfiguration(
        cpuProviderSelected(execution),
        "CLAP ONNX text backend currently supports only the CPU provider");
    requireConfiguration(
        execution.device_index < 0,
        "CLAP ONNX CPU text backend does not support device_index");
    requireConfiguration(
        execution.precision == PrecisionPreference::Auto ||
            execution.precision == PrecisionPreference::Float32,
        "CLAP ONNX text backend currently supports float32 execution only");
    requireConfiguration(
        execution.provider_options.empty(),
        "CLAP ONNX text backend does not currently expose provider_options");
}

void validateDescriptor(
    const std::optional<ModelDescriptor>& descriptor) {
    if (!descriptor.has_value() || descriptor->backend.empty()) {
        return;
    }
    requireConfiguration(
        descriptor->backend == "clap-onnx" ||
            descriptor->backend == "onnxruntime-clap" ||
            descriptor->backend == "clap",
        "ModelDescriptor backend must identify the CLAP ONNX backend");
}

ClapOnnxTextOptions validatedTextOptions(
    ClapOnnxTextOptions options) {
    validateDescriptor(options.model_descriptor);
    validateExecution(options.execution);
    requireConfiguration(
        !options.model.empty(),
        "CLAP ONNX text model path is required");
    requireConfiguration(
        !options.tokenizer.empty(),
        "CLAP tokenizer.json path is required");
    requireConfiguration(
        options.intra_op_threads > 0,
        "CLAP ONNX text intra_op_threads must be positive");
    requireConfiguration(
        options.sequence_length == kSequenceLength,
        "CLAP RoBERTa sequence length must be 77");
    requireConfiguration(
        options.embedding_dimension == kEmbeddingDimension,
        "CLAP text embedding dimension must be 512");
    requireConfiguration(
        !options.model_id.empty(),
        "CLAP text model_id must not be empty");
    return options;
}

std::filesystem::path resolveArtifact(
    const std::filesystem::path& artifact,
    const std::optional<ModelDescriptor>& descriptor) {
    if (artifact.empty() || artifact.is_absolute() ||
        !descriptor.has_value() ||
        descriptor->artifact_path.empty()) {
        return artifact;
    }
    return (descriptor->artifact_path / artifact).lexically_normal();
}

bool compatibleDimension(std::int64_t actual,
                         std::int64_t expected) {
    return actual <= 0 || actual == expected;
}

void validateTextInput(
    const Ort::Session& session,
    std::size_t index,
    const char* expected_name,
    std::size_t sequence_length) {
    Ort::AllocatorWithDefaultOptions allocator;
    const auto name =
        session.GetInputNameAllocated(index, allocator);
    if (std::string{name.get()} != expected_name) {
        throw Error{
            ErrorCode::ModelLoadError,
            std::string{"CLAP text model input "} +
                std::to_string(index) + " must be named '" +
                expected_name + "'"};
    }

    const auto type_info = session.GetInputTypeInfo(index);
    const auto tensor_info =
        type_info.GetTensorTypeAndShapeInfo();
    if (tensor_info.GetElementType() !=
        ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        throw Error{
            ErrorCode::ModelLoadError,
            std::string{"CLAP text input '"} +
                expected_name + "' must be int64"};
    }

    const auto shape = tensor_info.GetShape();
    if (shape.size() != 2U ||
        !compatibleDimension(shape[0], 1) ||
        !compatibleDimension(
            shape[1],
            static_cast<std::int64_t>(sequence_length))) {
        throw Error{
            ErrorCode::ModelLoadError,
            std::string{"CLAP text input '"} +
                expected_name +
                "' must have shape [batch,77] or compatible dynamic dimensions"};
    }
}

void validateOutput(
    const Ort::Session& session,
    std::size_t expected_dimension) {
    if (session.GetOutputCount() != 1U) {
        throw Error{
            ErrorCode::ModelLoadError,
            "CLAP text model must expose exactly one output"};
    }

    const auto type_info = session.GetOutputTypeInfo(0U);
    const auto tensor_info =
        type_info.GetTensorTypeAndShapeInfo();
    if (tensor_info.GetElementType() !=
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw Error{
            ErrorCode::ModelLoadError,
            "CLAP text output must be float32"};
    }

    const auto shape = tensor_info.GetShape();
    if (shape.size() == 1U) {
        if (!compatibleDimension(
                shape[0],
                static_cast<std::int64_t>(
                    expected_dimension))) {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP text output dimension does not match configuration"};
        }
        return;
    }

    if (shape.size() == 2U &&
        compatibleDimension(shape[0], 1) &&
        compatibleDimension(
            shape[1],
            static_cast<std::int64_t>(
                expected_dimension))) {
        return;
    }

    throw Error{
        ErrorCode::ModelLoadError,
        "CLAP text output must be [512] or [batch,512]"};
}

}  // namespace

}  // namespace audition::clap_detail

namespace audition {

void validateClapOnnxTextOptions(
    const ClapOnnxTextOptions& options) {
    static_cast<void>(
        clap_detail::validatedTextOptions(options));
}

}  // namespace audition

namespace audition::clap_detail {

class ClapTextEncoder::Impl {
public:
    explicit Impl(ClapOnnxTextOptions options) try
        : options_(validatedTextOptions(std::move(options))),
          tokenizer_(
              resolveArtifact(
                  options_.tokenizer,
                  options_.model_descriptor)),
          env_(ORT_LOGGING_LEVEL_WARNING,
               "libaudition-clap-text") {
        session_options_.SetIntraOpNumThreads(
            options_.intra_op_threads);
        session_options_.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

        const auto model_path =
            resolveArtifact(
                options_.model,
                options_.model_descriptor);
        session_ = std::make_unique<Ort::Session>(
            env_, model_path.c_str(), session_options_);

        if (session_->GetInputCount() != 2U) {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP text model must expose input_ids and attention_mask"};
        }

        validateTextInput(
            *session_, 0U, "input_ids",
            options_.sequence_length);
        validateTextInput(
            *session_, 1U, "attention_mask",
            options_.sequence_length);
        validateOutput(
            *session_, options_.embedding_dimension);
    } catch (const Ort::Exception& exception) {
        throw Error{
            ErrorCode::ModelLoadError,
            std::string{
                "Failed to initialize CLAP ONNX text model: "} +
                exception.what()};
    }

    [[nodiscard]] std::vector<float> embed(
        const std::string& text) const {
        const auto encoded =
            tokenizer_.encode(
                text, options_.sequence_length);

        const std::array<std::int64_t, 2U> shape{
            1,
            static_cast<std::int64_t>(
                options_.sequence_length)};

        auto ids = encoded.input_ids;
        auto mask = encoded.attention_mask;

        try {
            auto memory_info =
                Ort::MemoryInfo::CreateCpu(
                    OrtArenaAllocator,
                    OrtMemTypeDefault);

            auto ids_tensor =
                Ort::Value::CreateTensor<std::int64_t>(
                    memory_info,
                    ids.data(),
                    ids.size(),
                    shape.data(),
                    shape.size());
            auto mask_tensor =
                Ort::Value::CreateTensor<std::int64_t>(
                    memory_info,
                    mask.data(),
                    mask.size(),
                    shape.data(),
                    shape.size());

            std::array<Ort::Value, 2U> inputs{
                std::move(ids_tensor),
                std::move(mask_tensor)};
            const std::array<const char*, 2U> input_names{
                "input_ids",
                "attention_mask"};
            const std::array<const char*, 1U> output_names{
                "embedding"};

            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names.data(),
                inputs.data(),
                inputs.size(),
                output_names.data(),
                output_names.size());

            if (outputs.size() != 1U ||
                !outputs.front().IsTensor()) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "CLAP text model returned an invalid output"};
            }

            const auto output_info =
                outputs.front()
                    .GetTensorTypeAndShapeInfo();
            if (output_info.GetElementType() !=
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                output_info.GetElementCount() !=
                    options_.embedding_dimension) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "CLAP text output contract changed at runtime"};
            }

            const auto* data =
                outputs.front().GetTensorData<float>();
            std::vector<float> values(
                data,
                data + options_.embedding_dimension);

            double squared_norm = 0.0;
            for (const float value : values) {
                if (!std::isfinite(value)) {
                    throw Error{
                        ErrorCode::ProcessingError,
                        "CLAP text embedding contains a non-finite value"};
                }
                squared_norm +=
                    static_cast<double>(value) *
                    static_cast<double>(value);
            }

            if (!std::isfinite(squared_norm) ||
                squared_norm <=
                    std::numeric_limits<double>::epsilon()) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "CLAP text embedding has zero or invalid norm"};
            }

            return values;
        } catch (const Ort::Exception& exception) {
            throw Error{
                ErrorCode::ProcessingError,
                std::string{
                    "CLAP ONNX text inference failed: "} +
                    exception.what()};
        }
    }

    ClapOnnxTextOptions options_{};
    RobertaTokenizer tokenizer_;
    Ort::Env env_;
    Ort::SessionOptions session_options_{};
    std::unique_ptr<Ort::Session> session_{};
};

ClapTextEncoder::ClapTextEncoder(
    ClapOnnxTextOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(options))) {}

ClapTextEncoder::~ClapTextEncoder() = default;

ClapTextEncoder::ClapTextEncoder(
    ClapTextEncoder&&) noexcept = default;

ClapTextEncoder& ClapTextEncoder::operator=(
    ClapTextEncoder&&) noexcept = default;

std::vector<float> ClapTextEncoder::embed(
    const std::string& text) const {
    return impl_->embed(text);
}

const ClapOnnxTextOptions&
ClapTextEncoder::options() const noexcept {
    return impl_->options_;
}

std::size_t ClapTextEncoder::vocabularySize() const noexcept {
    return impl_->tokenizer_.vocabSize();
}

std::int64_t ClapTextEncoder::bosId() const noexcept {
    return impl_->tokenizer_.bosId();
}

std::int64_t ClapTextEncoder::eosId() const noexcept {
    return impl_->tokenizer_.eosId();
}

std::int64_t ClapTextEncoder::padId() const noexcept {
    return impl_->tokenizer_.padId();
}

std::int64_t ClapTextEncoder::unkId() const noexcept {
    return impl_->tokenizer_.unkId();
}

}  // namespace audition::clap_detail
