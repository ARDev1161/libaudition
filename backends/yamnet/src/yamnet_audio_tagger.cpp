#include <audition/backends/yamnet/audio_tagger.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>
#include <onnxruntime_cxx_api.h>

namespace audition {
namespace {

constexpr std::size_t kClassCount = 521U;
constexpr std::uint32_t kSampleRate = 16000U;

std::vector<std::string> readLabels(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) {
        throw Error{ErrorCode::ConfigurationError, "Cannot open YAMNet labels file"};
    }
    std::vector<std::string> labels;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // Official YAMNet CSV: index,mid,display_name. The name may include commas.
        const auto first = line.find(',');
        const auto second = first == std::string::npos ? first : line.find(',', first + 1U);
        if (first != std::string::npos && second != std::string::npos) {
            line = line.substr(second + 1U);
            if (line.size() > 1U && line.front() == '"' && line.back() == '"') {
                line = line.substr(1U, line.size() - 2U);
            }
        }
        if (line == "display_name" || line == "index,mid,display_name") {
            continue;
        }
        if (!line.empty()) {
            labels.push_back(std::move(line));
        }
    }
    if (labels.size() != kClassCount) {
        throw Error{ErrorCode::ConfigurationError,
                    "YAMNet requires exactly 521 class labels"};
    }
    return labels;
}

bool matchesDim(std::int64_t actual, std::int64_t expected) {
    return actual <= 0 || actual == expected;
}
} // namespace

class YamnetAudioTagger::Impl {
public:
    explicit Impl(YamnetOnnxOptions options)
        : options_(std::move(options)),
          labels_(readLabels(options_.labels)),
          env_(ORT_LOGGING_LEVEL_WARNING, "libaudition-yamnet") {
        if (options_.model.empty() || options_.top_k == 0U ||
            options_.top_k > kClassCount || options_.intra_op_threads <= 0) {
            throw Error{ErrorCode::ConfigurationError, "Invalid YAMNet options"};
        }
        session_options_.SetIntraOpNumThreads(options_.intra_op_threads);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        try {
            session_ = std::make_unique<Ort::Session>(
                env_, options_.model.c_str(), session_options_);
            if (session_->GetInputCount() != 1U || session_->GetOutputCount() < 1U) {
                throw Error{ErrorCode::ModelLoadError,
                            "YAMNet expects one waveform input and at least one output"};
            }
            Ort::AllocatorWithDefaultOptions allocator;
            auto name = session_->GetInputNameAllocated(0U, allocator);
            input_name_ = name.get();
            const auto info = session_->GetInputTypeInfo(0U).GetTensorTypeAndShapeInfo();
            const auto shape = info.GetShape();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                (shape.size() != 1U && shape.size() != 2U) ||
                (shape.size() == 2U && !matchesDim(shape[0], 1))) {
                std::string details = "YAMNet input mismatch: name=" + input_name_ +
                    ", element_type=" + std::to_string(static_cast<int>(info.GetElementType())) +
                    ", rank=" + std::to_string(shape.size()) + ", shape=[";
                for (std::size_t dim = 0; dim < shape.size(); ++dim) {
                    if (dim != 0U) details += ",";
                    details += std::to_string(shape[dim]);
                }
                details += "]; expected float32 waveform [N] or [1,N]";
                throw Error{ErrorCode::ModelLoadError, details};
            }
            input_rank_ = shape.size();
            fixed_samples_ = shape.back() > 0 ? static_cast<std::size_t>(shape.back()) : 0U;
            for (std::size_t i = 0; i < session_->GetOutputCount(); ++i) {
                const auto output_info = session_->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
                const auto dims = output_info.GetShape();
                if (output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                    (dims.size() != 2U && dims.size() != 3U) ||
                    !matchesDim(dims.back(), static_cast<std::int64_t>(kClassCount)) ||
                    (dims.size() == 3U && !matchesDim(dims[0], 1))) {
                    continue;
                }
                // Prefer the explicitly named scores output to embeddings/spectrogram.
                auto output_name = session_->GetOutputNameAllocated(i, allocator);
                if (output_index_ == -1 || std::string{output_name.get()}.find("score") != std::string::npos) {
                    output_index_ = static_cast<int>(i);
                    output_name_ = output_name.get();
                }
            }
            if (output_index_ < 0) {
                throw Error{ErrorCode::ModelLoadError,
                            "YAMNet ONNX export must contain float32 scores [...,521]"};
            }
        } catch (const Ort::Exception& exception) {
            throw Error{ErrorCode::ModelLoadError,
                        std::string{"Failed to load YAMNet model: "} + exception.what()};
        }
    }

    ClassificationResult classify(AudioView audio) const {
        if (!audio.format().valid() || audio.format().channel_count != 1U ||
            audio.format().sample_rate_hz != kSampleRate) {
            throw Error{ErrorCode::UnsupportedFormat, "YAMNet requires mono 16 kHz audio"};
        }
        if (audio.sampleCount() == 0U ||
            (fixed_samples_ != 0U && audio.sampleCount() != fixed_samples_)) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "YAMNet requires nonempty audio matching its fixed ONNX input length"};
        }
        std::vector<float> waveform(audio.data(), audio.data() + audio.sampleCount());
        if (!std::all_of(waveform.begin(), waveform.end(),
                         [](float x) { return std::isfinite(x); })) {
            throw Error{ErrorCode::InvalidArgument, "YAMNet received nonfinite audio"};
        }
        std::vector<std::int64_t> shape;
        if (input_rank_ == 2U) shape.push_back(1);
        shape.push_back(static_cast<std::int64_t>(waveform.size()));
        try {
            auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            auto tensor = Ort::Value::CreateTensor<float>(
                memory, waveform.data(), waveform.size(), shape.data(), shape.size());
            const char* inputs[]{input_name_.c_str()};
            const char* outputs[]{output_name_.c_str()};
            auto result = session_->Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1U, outputs, 1U);
            if (result.size() != 1U || !result[0].IsTensor()) {
                throw Error{ErrorCode::ProcessingError, "YAMNet did not return scores tensor"};
            }
            const auto info = result[0].GetTensorTypeAndShapeInfo();
            const auto dims = info.GetShape();
            const auto count = info.GetElementCount();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                dims.empty() || dims.back() != static_cast<std::int64_t>(kClassCount) ||
                count == 0U || count % kClassCount != 0U) {
                throw Error{ErrorCode::ProcessingError, "YAMNet output scores shape changed"};
            }
            const auto* data = result[0].GetTensorData<float>();
            const auto frames = count / kClassCount;
            std::array<double, kClassCount> mean{};
            for (std::size_t frame = 0; frame < frames; ++frame) {
                for (std::size_t cls = 0; cls < kClassCount; ++cls) {
                    const double score = static_cast<double>(data[frame * kClassCount + cls]);
                    if (!std::isfinite(score) || score < 0.0 || score > 1.0) {
                        throw Error{ErrorCode::ProcessingError,
                                    "YAMNet scores must be finite in [0,1]"};
                    }
                    mean[cls] += score / static_cast<double>(frames);
                }
            }
            std::vector<std::size_t> indices(kClassCount);
            std::iota(indices.begin(), indices.end(), 0U);
            std::partial_sort(indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(options_.top_k),
                              indices.end(), [&](std::size_t lhs, std::size_t rhs) {
                                  return mean[lhs] > mean[rhs];
                              });
            ClassificationResult output{};
            for (std::size_t i = 0; i < options_.top_k; ++i) {
                const auto cls = indices[i];
                output.classes.push_back({labels_[cls], Probability::from(mean[cls])});
            }
            return output;
        } catch (const Ort::Exception& exception) {
            throw Error{ErrorCode::ProcessingError,
                        std::string{"YAMNet inference failed: "} + exception.what()};
        }
    }

    YamnetOnnxOptions options_;
    std::vector<std::string> labels_;
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::string output_name_;
    std::size_t input_rank_{0};
    std::size_t fixed_samples_{0};
    int output_index_{-1};
};

YamnetAudioTagger::YamnetAudioTagger(YamnetOnnxOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
YamnetAudioTagger::~YamnetAudioTagger() = default;
YamnetAudioTagger::YamnetAudioTagger(YamnetAudioTagger&&) noexcept = default;
YamnetAudioTagger& YamnetAudioTagger::operator=(YamnetAudioTagger&&) noexcept = default;

BackendInfo YamnetAudioTagger::backendInfo() const {
    return {"yamnet-onnx", OrtGetApiBase()->GetVersionString()};
}
ClassifierCapabilities YamnetAudioTagger::capabilities() const {
    ClassifierCapabilities result{};
    result.audio.supported_sample_rates_hz = {kSampleRate};
    result.audio.min_channels = 1U;
    result.audio.max_channels = 1U;
    result.audio.preferred_frame_count = impl_->fixed_samples_ != 0U
        ? impl_->fixed_samples_ : 15600U;
    result.execution.device_classes = {DeviceClass::Cpu};
    result.execution.providers = {"cpu"};
    return result;
}
ClassificationResult YamnetAudioTagger::classify(AudioView audio) const {
    return impl_->classify(audio);
}

} // namespace audition
