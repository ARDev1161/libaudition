#include <audition/backends/sherpa/offline_asr.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"

namespace audition {
namespace {

ExecutionCapabilities executionCapabilities(const SherpaRuntimeOptions& runtime) {
    ExecutionCapabilities capabilities{};
    capabilities.device_classes = {runtime.execution.device_class};
    capabilities.providers = {sherpa_detail::providerFor(runtime)};
    return capabilities;
}

}  // namespace

class SherpaOfflineAsr::Impl {
public:
    explicit Impl(SherpaOfflineAsrOptions options)
        : options_(std::move(options)),
          config_(sherpa_detail::makeOfflineAsrConfig(options_)),
          recognizer_(sherpa_onnx::cxx::OfflineRecognizer::Create(config_)) {
        if (recognizer_.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx offline recognizer"};
        }
    }

    Transcript transcribe(const SpeechSegment& segment) const {
        sherpa_detail::validateMonoAudio(
            segment.audio.view(), options_.features.sample_rate_hz, "offline ASR");
        if (segment.audio.sampleCount() == 0U) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa offline ASR requires non-empty audio"};
        }
        if (segment.audio.sampleCount() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa offline ASR input exceeds backend range"};
        }

        std::lock_guard<std::mutex> lock{mutex_};
        auto stream = options_.hotwords.empty()
                          ? recognizer_.CreateStream()
                          : recognizer_.CreateStream(options_.hotwords);
        if (stream.Get() == nullptr) {
            throw Error{ErrorCode::ProcessingError,
                        "Failed to create sherpa-onnx offline stream"};
        }

        stream.AcceptWaveform(
            static_cast<std::int32_t>(segment.audio.format().sample_rate_hz),
            segment.audio.data(),
            static_cast<std::int32_t>(segment.audio.sampleCount()));
        recognizer_.Decode(&stream);
        return sherpa_detail::transcriptFromOffline(
            recognizer_.GetResult(&stream), segment.segment_id);
    }

    SherpaOfflineAsrOptions options_{};
    sherpa_onnx::cxx::OfflineRecognizerConfig config_{};
    sherpa_onnx::cxx::OfflineRecognizer recognizer_;
    mutable std::mutex mutex_{};
};

SherpaOfflineAsr::SherpaOfflineAsr(SherpaOfflineAsrOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaOfflineAsr::~SherpaOfflineAsr() = default;
SherpaOfflineAsr::SherpaOfflineAsr(SherpaOfflineAsr&&) noexcept = default;
SherpaOfflineAsr& SherpaOfflineAsr::operator=(SherpaOfflineAsr&&) noexcept = default;

BackendInfo SherpaOfflineAsr::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

AsrCapabilities SherpaOfflineAsr::capabilities() const {
    AsrCapabilities capabilities{};
    capabilities.token_timestamps =
        sherpa_detail::offlineTokenTimestampsConfigured(impl_->options_);
    capabilities.word_timestamps = false;
    capabilities.language_identification =
        sherpa_detail::offlineLanguageIdentificationExpected(impl_->options_);
    capabilities.partial_results = false;
    capabilities.audio =
        sherpa_detail::monoRequirements(impl_->options_.features.sample_rate_hz);
    capabilities.execution = executionCapabilities(impl_->options_.runtime);
    return capabilities;
}

Transcript SherpaOfflineAsr::transcribe(const SpeechSegment& segment) const {
    return impl_->transcribe(segment);
}

const SherpaOfflineAsrOptions& SherpaOfflineAsr::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
