#include <audition/backends/clap/audio_embedder.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

#include <onnxruntime_cxx_api.h>

namespace audition {
namespace {

void requireConfiguration(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

void validateDescriptor(const std::optional<ModelDescriptor>& descriptor) {
    if (!descriptor.has_value() || descriptor->backend.empty()) {
        return;
    }
    requireConfiguration(
        descriptor->backend == "clap-onnx" ||
            descriptor->backend == "onnxruntime-clap" ||
            descriptor->backend == "clap",
        "ModelDescriptor backend must identify the CLAP ONNX backend");
}

std::filesystem::path resolvedModelPath(
    const ClapOnnxAudioOptions& options) {
    if (options.model.empty() || options.model.is_absolute() ||
        !options.model_descriptor.has_value() ||
        options.model_descriptor->artifact_path.empty()) {
        return options.model;
    }
    return (options.model_descriptor->artifact_path / options.model)
        .lexically_normal();
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
        "CLAP ONNX audio backend currently supports CPU execution only");
    requireConfiguration(
        cpuProviderSelected(execution),
        "CLAP ONNX audio backend currently supports only the CPU provider");
    requireConfiguration(
        execution.device_index < 0,
        "CLAP ONNX CPU backend does not support device_index");
    requireConfiguration(
        execution.precision == PrecisionPreference::Auto ||
            execution.precision == PrecisionPreference::Float32,
        "CLAP ONNX audio backend currently supports float32 execution only");
    requireConfiguration(
        execution.provider_options.empty(),
        "CLAP ONNX audio backend does not currently expose provider_options");
}

void validateAudio(AudioView audio, std::uint32_t sample_rate_hz) {
    if (!audio.format().valid()) {
        throw Error{ErrorCode::InvalidArgument,
                    "CLAP audio embedding requires a valid audio format"};
    }
    if (audio.format().channel_count != 1U ||
        audio.format().sample_rate_hz != sample_rate_hz) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "CLAP audio embedding requires mono audio at the configured sample rate"};
    }
    if (audio.sampleCount() == 0U) {
        throw Error{ErrorCode::InvalidArgument,
                    "CLAP audio embedding requires non-empty audio"};
    }
    if (audio.sampleCount() >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw Error{ErrorCode::InvalidArgument,
                    "CLAP audio input exceeds ONNX Runtime tensor limits"};
    }
}

std::optional<std::size_t> fixedFrameCount(
    const std::vector<std::int64_t>& shape) {
    if (shape.size() == 1U) {
        if (shape[0] > 0) {
            return static_cast<std::size_t>(shape[0]);
        }
        return std::nullopt;
    }
    if (shape.size() == 2U) {
        if (shape[0] > 0 && shape[0] != 1) {
            throw Error{ErrorCode::ModelLoadError,
                        "CLAP audio model batch dimension must be one or dynamic"};
        }
        if (shape[1] > 0) {
            return static_cast<std::size_t>(shape[1]);
        }
        return std::nullopt;
    }
    throw Error{ErrorCode::ModelLoadError,
                "CLAP audio model input must be rank 1 or rank 2"};
}

void validateOutputShape(const std::vector<std::int64_t>& shape,
                         std::size_t expected_dimension) {
    if (shape.size() == 1U) {
        if (shape[0] > 0 &&
            static_cast<std::size_t>(shape[0]) != expected_dimension) {
            throw Error{ErrorCode::ModelLoadError,
                        "CLAP audio model output dimension does not match configuration"};
        }
        return;
    }
    if (shape.size() == 2U) {
        if (shape[0] > 0 && shape[0] != 1) {
            throw Error{ErrorCode::ModelLoadError,
                        "CLAP audio model output batch dimension must be one or dynamic"};
        }
        if (shape[1] > 0 &&
            static_cast<std::size_t>(shape[1]) != expected_dimension) {
            throw Error{ErrorCode::ModelLoadError,
                        "CLAP audio model output dimension does not match configuration"};
        }
        return;
    }
    throw Error{ErrorCode::ModelLoadError,
                "CLAP audio model output must be rank 1 or rank 2"};
}

}  // namespace

void validateClapOnnxAudioOptions(const ClapOnnxAudioOptions& options) {
    validateDescriptor(options.model_descriptor);
    validateExecution(options.execution);
    requireConfiguration(!options.model.empty(),
                         "CLAP ONNX audio model path is required");
    requireConfiguration(options.intra_op_threads > 0,
                         "CLAP ONNX intra_op_threads must be positive");
    requireConfiguration(options.sample_rate_hz > 0U,
                         "CLAP ONNX sample rate must be positive");
    requireConfiguration(options.embedding_dimension > 0U,
                         "CLAP ONNX embedding dimension must be positive");
    requireConfiguration(!options.model_id.empty(),
                         "CLAP ONNX model_id must not be empty");
}

class ClapAudioEmbedder::Impl {
public:
    explicit Impl(ClapOnnxAudioOptions options) try
        : options_(std::move(options)),
          env_(ORT_LOGGING_LEVEL_WARNING, "libaudition-clap") {
        validateClapOnnxAudioOptions(options_);

        session_options_.SetIntraOpNumThreads(options_.intra_op_threads);
        session_options_.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

        const auto model_path = resolvedModelPath(options_);
        session_ = std::make_unique<Ort::Session>(
            env_, model_path.c_str(), session_options_);

        if (session_->GetInputCount() != 1U ||
            session_->GetOutputCount() != 1U) {
            throw Error{ErrorCode::ModelLoadError,
                        "CLAP audio model must expose exactly one input and one output"};
        }

        Ort::AllocatorWithDefaultOptions allocator;
        const auto input_name =
            session_->GetInputNameAllocated(0U, allocator);
        const auto output_name =
            session_->GetOutputNameAllocated(0U, allocator);
        input_name_ = input_name.get();
        output_name_ = output_name.get();

        const auto input_info =
            session_->GetInputTypeInfo(0U).GetTensorTypeAndShapeInfo();
        if (input_info.GetElementType() !=
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw Error{ErrorCode::ModelLoadError,
                        "CLAP audio model input tensor must be float32"};
        }
        input_shape_ = input_info.GetShape();
        fixed_frame_count_ = fixedFrameCount(input_shape_);

        const auto output_info =
            session_->GetOutputTypeInfo(0U).GetTensorTypeAndShapeInfo();
        if (output_info.GetElementType() !=
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw Error{ErrorCode::ModelLoadError,
                        "CLAP audio model output tensor must be float32"};
        }
        validateOutputShape(
            output_info.GetShape(), options_.embedding_dimension);
    } catch (const Ort::Exception& exception) {
        throw Error{
            ErrorCode::ModelLoadError,
            std::string{"Failed to initialize CLAP ONNX audio model: "} +
                exception.what()};
    }

    [[nodiscard]] AudioEmbedding embed(AudioView audio) const {
        validateAudio(audio, options_.sample_rate_hz);
        if (fixed_frame_count_.has_value() &&
            audio.frameCount() != *fixed_frame_count_) {
            throw Error{
                ErrorCode::UnsupportedFormat,
                "CLAP audio model requires an exact frame count; segment or pad explicitly"};
        }

        std::vector<float> input_samples(
            audio.data(), audio.data() + audio.sampleCount());

        std::vector<std::int64_t> input_shape;
        if (input_shape_.size() == 1U) {
            input_shape = {
                static_cast<std::int64_t>(audio.sampleCount())};
        } else {
            input_shape = {
                1,
                static_cast<std::int64_t>(audio.frameCount())};
        }

        try {
            auto memory_info = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);
            auto input_tensor = Ort::Value::CreateTensor<float>(
                memory_info,
                input_samples.data(),
                input_samples.size(),
                input_shape.data(),
                input_shape.size());

            const std::array<const char*, 1U> input_names{
                input_name_.c_str()};
            const std::array<const char*, 1U> output_names{
                output_name_.c_str()};

            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names.data(),
                &input_tensor,
                input_names.size(),
                output_names.data(),
                output_names.size());

            if (outputs.size() != 1U || !outputs.front().IsTensor()) {
                throw Error{ErrorCode::ProcessingError,
                            "CLAP ONNX audio model returned an invalid output"};
            }

            const auto output_info =
                outputs.front().GetTensorTypeAndShapeInfo();
            if (output_info.GetElementType() !=
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                throw Error{ErrorCode::ProcessingError,
                            "CLAP ONNX audio output is not float32"};
            }

            const auto count = output_info.GetElementCount();
            if (count != options_.embedding_dimension) {
                throw Error{ErrorCode::ProcessingError,
                            "CLAP ONNX audio output dimension changed at runtime"};
            }

            const float* data = outputs.front().GetTensorData<float>();
            std::vector<float> values(data, data + count);

            double squared_norm = 0.0;
            for (float value : values) {
                if (!std::isfinite(value)) {
                    throw Error{ErrorCode::ProcessingError,
                                "CLAP ONNX audio embedding contains a non-finite value"};
                }
                squared_norm += static_cast<double>(value) *
                                static_cast<double>(value);
            }
            if (!std::isfinite(squared_norm) ||
                squared_norm <= std::numeric_limits<double>::epsilon()) {
                throw Error{ErrorCode::ProcessingError,
                            "CLAP ONNX audio embedding has zero or invalid norm"};
            }

            return AudioEmbedding{
                options_.model_id, std::move(values), std::nullopt};
        } catch (const Ort::Exception& exception) {
            throw Error{
                ErrorCode::ProcessingError,
                std::string{"CLAP ONNX audio inference failed: "} +
                    exception.what()};
        }
    }

    ClapOnnxAudioOptions options_{};
    Ort::Env env_;
    Ort::SessionOptions session_options_{};
    std::unique_ptr<Ort::Session> session_{};
    std::string input_name_{};
    std::string output_name_{};
    std::vector<std::int64_t> input_shape_{};
    std::optional<std::size_t> fixed_frame_count_{};
};

ClapAudioEmbedder::ClapAudioEmbedder(ClapOnnxAudioOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
ClapAudioEmbedder::~ClapAudioEmbedder() = default;
ClapAudioEmbedder::ClapAudioEmbedder(ClapAudioEmbedder&&) noexcept = default;
ClapAudioEmbedder& ClapAudioEmbedder::operator=(
    ClapAudioEmbedder&&) noexcept = default;

BackendInfo ClapAudioEmbedder::backendInfo() const {
    return {"clap-onnx", OrtGetApiBase()->GetVersionString()};
}

EmbeddingCapabilities ClapAudioEmbedder::capabilities() const {
    EmbeddingCapabilities capabilities{};
    capabilities.audio.supported_sample_rates_hz = {
        impl_->options_.sample_rate_hz};
    capabilities.audio.min_channels = 1U;
    capabilities.audio.max_channels = 1U;
    capabilities.audio.supported_layouts = {
        AudioLayout::Interleaved, AudioLayout::Planar};
    capabilities.audio.preferred_frame_count =
        impl_->fixed_frame_count_;
    capabilities.execution.device_classes = {DeviceClass::Cpu};
    capabilities.execution.providers = {"cpu"};
    capabilities.embedding_dimension =
        impl_->options_.embedding_dimension;
    return capabilities;
}

AudioEmbedding ClapAudioEmbedder::embed(AudioView audio) const {
    return impl_->embed(audio);
}

const ClapOnnxAudioOptions& ClapAudioEmbedder::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
