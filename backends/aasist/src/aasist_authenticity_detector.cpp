#include <audition/backends/aasist/authenticity_detector.hpp>

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

constexpr std::uint32_t kAasistSampleRateHz = 16000U;
constexpr std::size_t kAasistFrameCount = 64600U;

void requireConfiguration(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

void validateDescriptor(
    const std::optional<ModelDescriptor>& descriptor) {
    if (!descriptor.has_value() || descriptor->backend.empty()) {
        return;
    }
    requireConfiguration(
        descriptor->backend == "aasist-onnx" ||
            descriptor->backend == "onnxruntime-aasist" ||
            descriptor->backend == "aasist",
        "ModelDescriptor backend must identify the AASIST ONNX backend");
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
        "AASIST ONNX backend currently supports CPU execution only");
    requireConfiguration(
        cpuProviderSelected(execution),
        "AASIST ONNX backend currently supports only the CPU provider");
    requireConfiguration(
        execution.device_index < 0,
        "AASIST ONNX CPU backend does not support device_index");
    requireConfiguration(
        execution.precision == PrecisionPreference::Auto ||
            execution.precision == PrecisionPreference::Float32,
        "AASIST ONNX backend currently supports float32 execution only");
    requireConfiguration(
        execution.provider_options.empty(),
        "AASIST ONNX backend does not currently expose provider_options");
}

std::filesystem::path resolvedModelPath(
    const AasistOnnxOptions& options) {
    if (options.model.empty() ||
        options.model.is_absolute() ||
        !options.model_descriptor.has_value() ||
        options.model_descriptor->artifact_path.empty()) {
        return options.model;
    }
    return (options.model_descriptor->artifact_path / options.model)
        .lexically_normal();
}

bool compatibleDimension(
    std::int64_t actual,
    std::int64_t expected) {
    return actual <= 0 || actual == expected;
}

void validateAudio(
    AudioView audio,
    const AasistOnnxOptions& options) {
    if (!audio.format().valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AASIST requires a valid audio format"};
    }
    if (audio.format().sample_rate_hz !=
            options.sample_rate_hz ||
        audio.format().channel_count != 1U) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "AASIST requires mono 16 kHz audio"};
    }
    if (audio.sampleCount() == 0U) {
        throw Error{
            ErrorCode::InvalidArgument,
            "AASIST requires non-empty speech audio"};
    }
    if (audio.frameCount() > options.frame_count) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "AASIST accepts at most 64600 frames per call; segment longer speech explicitly"};
    }
}

std::vector<float> prepareWindow(
    AudioView audio,
    std::size_t frame_count) {
    std::vector<float> window(frame_count);

    for (std::size_t i = 0U;
         i < audio.sampleCount();
         ++i) {
        if (!std::isfinite(audio.data()[i])) {
            throw Error{
                ErrorCode::InvalidArgument,
                "AASIST input contains a non-finite sample"};
        }
    }

    for (std::size_t i = 0U;
         i < frame_count;
         ++i) {
        window[i] =
            audio.data()[i % audio.sampleCount()];
    }
    return window;
}

double stableSigmoid(double value) {
    if (value >= 0.0) {
        return 1.0 / (1.0 + std::exp(-value));
    }
    const double exponential = std::exp(value);
    return exponential / (1.0 + exponential);
}

}  // namespace

void validateAasistOnnxOptions(
    const AasistOnnxOptions& options) {
    validateDescriptor(options.model_descriptor);
    validateExecution(options.execution);

    requireConfiguration(
        !options.model.empty(),
        "AASIST ONNX model path is required");
    requireConfiguration(
        options.intra_op_threads > 0,
        "AASIST intra_op_threads must be positive");
    requireConfiguration(
        options.sample_rate_hz == kAasistSampleRateHz,
        "AASIST official model requires 16 kHz audio");
    requireConfiguration(
        options.frame_count == kAasistFrameCount,
        "AASIST official model requires a 64600-frame analysis window");

    if (options.calibration.has_value()) {
        requireConfiguration(
            std::isfinite(options.calibration->slope) &&
                std::isfinite(options.calibration->intercept),
            "AASIST calibration coefficients must be finite");
        requireConfiguration(
            options.calibration->slope != 0.0,
            "AASIST calibration slope must be non-zero");
    }
}

class AasistAuthenticityDetector::Impl {
public:
    explicit Impl(AasistOnnxOptions options) try
        : options_(std::move(options)),
          env_(ORT_LOGGING_LEVEL_WARNING,
               "libaudition-aasist") {
        validateAasistOnnxOptions(options_);

        session_options_.SetIntraOpNumThreads(
            options_.intra_op_threads);
        session_options_.SetInterOpNumThreads(1);
        session_options_.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

        const auto model_path =
            resolvedModelPath(options_);
        session_ = std::make_unique<Ort::Session>(
            env_, model_path.c_str(), session_options_);

        if (session_->GetInputCount() != 1U ||
            session_->GetOutputCount() != 1U) {
            throw Error{
                ErrorCode::ModelLoadError,
                "AASIST model must expose one waveform input and one logits output"};
        }

        Ort::AllocatorWithDefaultOptions allocator;
        const auto input_name =
            session_->GetInputNameAllocated(
                0U, allocator);
        input_name_ = input_name.get();
        if (input_name_ != "wav") {
            throw Error{
                ErrorCode::ModelLoadError,
                "AASIST official ONNX input must be named 'wav'"};
        }

        const auto input_type_info =
            session_->GetInputTypeInfo(0U);
        const auto input_info =
            input_type_info.GetTensorTypeAndShapeInfo();
        if (input_info.GetElementType() !=
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw Error{
                ErrorCode::ModelLoadError,
                "AASIST waveform input must be float32"};
        }

        const auto input_shape = input_info.GetShape();
        if (input_shape.size() != 2U ||
            !compatibleDimension(input_shape[0], 1) ||
            !compatibleDimension(
                input_shape[1],
                static_cast<std::int64_t>(
                    options_.frame_count))) {
            throw Error{
                ErrorCode::ModelLoadError,
                "AASIST input must have shape [batch,64600]"};
        }

        const auto output_name =
            session_->GetOutputNameAllocated(
                0U, allocator);
        output_name_ = output_name.get();

        const auto output_type_info =
            session_->GetOutputTypeInfo(0U);
        const auto output_info =
            output_type_info.GetTensorTypeAndShapeInfo();
        if (output_info.GetElementType() !=
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw Error{
                ErrorCode::ModelLoadError,
                "AASIST logits output must be float32"};
        }

        const auto output_shape = output_info.GetShape();
        if (output_shape.size() != 2U ||
            !compatibleDimension(output_shape[0], 1) ||
            !compatibleDimension(output_shape[1], 2)) {
            throw Error{
                ErrorCode::ModelLoadError,
                "AASIST output must have shape [batch,2]"};
        }
    } catch (const Ort::Exception& exception) {
        throw Error{
            ErrorCode::ModelLoadError,
            std::string{
                "Failed to initialize AASIST ONNX model: "} +
                exception.what()};
    }

    [[nodiscard]] AuthenticityResult analyze(
        AudioView speech) const {
        validateAudio(speech, options_);
        auto window =
            prepareWindow(
                speech, options_.frame_count);

        const std::array<std::int64_t, 2U> shape{
            1,
            static_cast<std::int64_t>(
                options_.frame_count)};

        try {
            auto memory_info =
                Ort::MemoryInfo::CreateCpu(
                    OrtArenaAllocator,
                    OrtMemTypeDefault);
            auto input =
                Ort::Value::CreateTensor<float>(
                    memory_info,
                    window.data(),
                    window.size(),
                    shape.data(),
                    shape.size());

            const std::array<const char*, 1U>
                input_names{input_name_.c_str()};
            const std::array<const char*, 1U>
                output_names{output_name_.c_str()};

            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names.data(),
                &input,
                input_names.size(),
                output_names.data(),
                output_names.size());

            if (outputs.size() != 1U ||
                !outputs.front().IsTensor()) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "AASIST returned an invalid logits output"};
            }

            const auto output_info =
                outputs.front()
                    .GetTensorTypeAndShapeInfo();
            if (output_info.GetElementType() !=
                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                output_info.GetElementCount() != 2U) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "AASIST runtime output contract changed"};
            }

            const auto* logits =
                outputs.front().GetTensorData<float>();
            const double spoof =
                static_cast<double>(logits[0]);
            const double bona_fide =
                static_cast<double>(logits[1]);

            if (!std::isfinite(spoof) ||
                !std::isfinite(bona_fide)) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "AASIST returned non-finite logits"};
            }

            AuthenticityResult result{};
            result.spoof_score = Score{spoof};
            result.bona_fide_score =
                Score{bona_fide};

            if (options_.calibration.has_value()) {
                const double calibrated_logit =
                    options_.calibration->slope *
                        bona_fide +
                    options_.calibration->intercept;
                const double bona_fide_probability =
                    stableSigmoid(calibrated_logit);

                result.bona_fide_probability =
                    Probability::from(
                        bona_fide_probability);
                result.spoof_probability =
                    Probability::from(
                        1.0 - bona_fide_probability);
            }

            return result;
        } catch (const Ort::Exception& exception) {
            throw Error{
                ErrorCode::ProcessingError,
                std::string{
                    "AASIST ONNX inference failed: "} +
                    exception.what()};
        }
    }

    AasistOnnxOptions options_{};
    Ort::Env env_;
    Ort::SessionOptions session_options_{};
    std::unique_ptr<Ort::Session> session_{};
    std::string input_name_{};
    std::string output_name_{};
};

AasistAuthenticityDetector::
    AasistAuthenticityDetector(
        AasistOnnxOptions options)
    : impl_(
          std::make_unique<Impl>(
              std::move(options))) {}

AasistAuthenticityDetector::
    ~AasistAuthenticityDetector() = default;

AasistAuthenticityDetector::
    AasistAuthenticityDetector(
        AasistAuthenticityDetector&&) noexcept =
    default;

AasistAuthenticityDetector&
AasistAuthenticityDetector::operator=(
    AasistAuthenticityDetector&&) noexcept =
    default;

BackendInfo
AasistAuthenticityDetector::backendInfo() const {
    return {
        "aasist-onnx",
        OrtGetApiBase()->GetVersionString()};
}

AuthenticityCapabilities
AasistAuthenticityDetector::capabilities() const {
    AuthenticityCapabilities capabilities{};
    capabilities.calibrated_probability =
        impl_->options_.calibration.has_value();
    capabilities.replay_attribution = false;
    capabilities.synthetic_attribution = false;
    capabilities.audio.supported_sample_rates_hz = {
        impl_->options_.sample_rate_hz};
    capabilities.audio.min_channels = 1U;
    capabilities.audio.max_channels = 1U;
    capabilities.audio.supported_layouts = {
        AudioLayout::Interleaved,
        AudioLayout::Planar};
    capabilities.audio.preferred_frame_count =
        impl_->options_.frame_count;
    capabilities.execution.device_classes = {
        DeviceClass::Cpu};
    capabilities.execution.providers = {"cpu"};
    return capabilities;
}

AuthenticityResult
AasistAuthenticityDetector::analyze(
    AudioView speech) const {
    return impl_->analyze(speech);
}

const AasistOnnxOptions&
AasistAuthenticityDetector::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
