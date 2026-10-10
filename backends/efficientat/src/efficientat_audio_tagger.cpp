#include <audition/backends/efficientat/audio_tagger.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>
#include <onnxruntime_cxx_api.h>

namespace audition {
namespace {
constexpr std::size_t kMelBins = 128U;
constexpr std::size_t kClassCount = 527U;
bool sameOrDynamic(std::int64_t actual, std::int64_t expected) {
    return actual <= 0 || actual == expected;
}
std::vector<std::string> loadLabels(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) throw Error{ErrorCode::ConfigurationError, "Cannot open EfficientAT labels"};
    std::vector<std::string> labels;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) labels.push_back(std::move(line));
    }
    if (labels.size() != kClassCount) {
        throw Error{ErrorCode::ConfigurationError,
                    "EfficientAT requires 527 ordered AudioSet labels"};
    }
    return labels;
}
double sigmoid(double x) {
    if (x >= 0.0) return 1.0 / (1.0 + std::exp(-x));
    const double e = std::exp(x);
    return e / (1.0 + e);
}
}

class EfficientAtSpectrogramTagger::Impl {
public:
    explicit Impl(EfficientAtOnnxOptions options)
        : options_(std::move(options)), labels_(loadLabels(options_.labels)),
          env_(ORT_LOGGING_LEVEL_WARNING, "libaudition-efficientat") {
        if (options_.model.empty() || options_.intra_op_threads < 1 ||
            options_.top_k == 0U || options_.top_k > kClassCount) {
            throw Error{ErrorCode::ConfigurationError, "Invalid EfficientAT options"};
        }
        session_options_.SetIntraOpNumThreads(options_.intra_op_threads);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        try {
            session_ = std::make_unique<Ort::Session>(env_, options_.model.c_str(), session_options_);
            if (session_->GetInputCount() != 1U) {
                throw Error{ErrorCode::ModelLoadError, "EfficientAT must have one log-mel input"};
            }
            const auto input_type = session_->GetInputTypeInfo(0U);
            const auto info = input_type.GetTensorTypeAndShapeInfo();
            const auto input_shape = info.GetShape();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                input_shape.size() != 4U ||
                !sameOrDynamic(input_shape[0], 1) ||
                !sameOrDynamic(input_shape[1], 1) ||
                !sameOrDynamic(input_shape[2], static_cast<std::int64_t>(kMelBins))) {
                throw Error{ErrorCode::ModelLoadError,
                            "EfficientAT requires float32 log-mel input [1,1,128,T]"};
            }
            fixed_frames_ = input_shape[3] > 0 ? static_cast<std::size_t>(input_shape[3]) : 0U;
            Ort::AllocatorWithDefaultOptions allocator;
            auto name = session_->GetInputNameAllocated(0U, allocator);
            input_name_ = name.get();

            for (std::size_t i = 0U; i < session_->GetOutputCount(); ++i) {
                const auto output_type = session_->GetOutputTypeInfo(i);
                const auto output_info = output_type.GetTensorTypeAndShapeInfo();
                const auto shape = output_info.GetShape();
                if (output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                    (shape.size() != 1U && shape.size() != 2U) ||
                    !sameOrDynamic(shape.back(), static_cast<std::int64_t>(kClassCount)) ||
                    (shape.size() == 2U && !sameOrDynamic(shape[0], 1))) {
                    continue;
                }
                Ort::AllocatorWithDefaultOptions output_allocator;
                auto output_name = session_->GetOutputNameAllocated(i, output_allocator);
                output_name_ = output_name.get();
                output_rank_ = shape.size();
                output_index_ = i;
                break;
            }
            if (output_name_.empty()) {
                throw Error{ErrorCode::ModelLoadError,
                            "EfficientAT requires float32 logits output [527] or [1,527]"};
            }
        } catch (const Ort::Exception& exception) {
            throw Error{ErrorCode::ModelLoadError,
                        std::string{"Failed to load EfficientAT ONNX: "} + exception.what()};
        }
    }

    ClassificationResult classify(const float* mel, std::size_t bins, std::size_t frames) const {
        if (mel == nullptr || bins != kMelBins || frames == 0U ||
            (fixed_frames_ != 0U && fixed_frames_ != frames) ||
            frames > std::numeric_limits<std::size_t>::max() / kMelBins) {
            throw Error{ErrorCode::InvalidArgument,
                        "Invalid EfficientAT log-mel tensor [1,1,128,T]"};
        }
        const std::size_t count = bins * frames;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(mel[i])) {
                throw Error{ErrorCode::InvalidArgument, "Nonfinite EfficientAT log-mel sample"};
            }
        }
        const std::vector<std::int64_t> shape{1, 1, static_cast<std::int64_t>(bins),
                                              static_cast<std::int64_t>(frames)};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(memory, const_cast<float*>(mel),
                                                      count, shape.data(), shape.size());
        const char* input_names[] = {input_name_.c_str()};
        const char* output_names[] = {output_name_.c_str()};
        std::vector<Ort::Value> outputs;
        try {
            outputs = session_->Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1U,
                                    output_names, 1U);
        } catch (const Ort::Exception& e) {
            throw Error{ErrorCode::ProcessingError,
                        std::string{"EfficientAT ONNX inference failed: "} + e.what()};
        }
        if (outputs.size() != 1U || !outputs[0].IsTensor()) {
            throw Error{ErrorCode::ProcessingError, "EfficientAT output is not a tensor"};
        }
        const auto shape_info = outputs[0].GetTensorTypeAndShapeInfo();
        if (shape_info.GetElementCount() != kClassCount ||
            shape_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw Error{ErrorCode::ProcessingError, "EfficientAT output must have 527 logits"};
        }
        const float* logits = outputs[0].GetTensorData<float>();
        ClassificationResult result{};
        result.classes.reserve(kClassCount);
        for (std::size_t i = 0; i < kClassCount; ++i) {
            if (!std::isfinite(logits[i])) {
                throw Error{ErrorCode::ProcessingError, "Nonfinite EfficientAT logit"};
            }
            result.classes.push_back({labels_[i], Probability::from(sigmoid(logits[i]))});
        }
        std::stable_sort(result.classes.begin(), result.classes.end(),
            [](const ClassScore& a, const ClassScore& b) {
                return a.probability.value() > b.probability.value();
            });
        result.classes.resize(options_.top_k);
        return result;
    }
private:
    EfficientAtOnnxOptions options_{};
    std::vector<std::string> labels_{};
    Ort::Env env_;
    Ort::SessionOptions session_options_{};
    std::unique_ptr<Ort::Session> session_{};
    std::string input_name_{};
    std::string output_name_{};
    std::size_t output_index_{0U};
    std::size_t output_rank_{0U};
    std::size_t fixed_frames_{0U};
};

EfficientAtSpectrogramTagger::EfficientAtSpectrogramTagger(EfficientAtOnnxOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
EfficientAtSpectrogramTagger::~EfficientAtSpectrogramTagger() = default;
EfficientAtSpectrogramTagger::EfficientAtSpectrogramTagger(EfficientAtSpectrogramTagger&&) noexcept = default;
EfficientAtSpectrogramTagger& EfficientAtSpectrogramTagger::operator=(EfficientAtSpectrogramTagger&&) noexcept = default;
ClassificationResult EfficientAtSpectrogramTagger::classifyLogMel(
    const float* mel, std::size_t bins, std::size_t frames) const {
    return impl_->classify(mel, bins, frames);
}
} // namespace audition
