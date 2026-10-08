#include <audition/backends/sherpa/streaming_asr.hpp>

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

struct StreamingSharedState {
    explicit StreamingSharedState(const SherpaStreamingAsrOptions& requested_options)
        : options(requested_options),
          config(sherpa_detail::makeStreamingAsrConfig(requested_options)),
          recognizer(sherpa_onnx::cxx::OnlineRecognizer::Create(config)) {
        if (recognizer.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx streaming recognizer"};
        }
    }

    SherpaStreamingAsrOptions options{};
    sherpa_onnx::cxx::OnlineRecognizerConfig config{};
    sherpa_onnx::cxx::OnlineRecognizer recognizer;
    mutable std::mutex mutex{};
};

class SherpaStreamingSession final : public IStreamingAsrSession {
public:
    explicit SherpaStreamingSession(std::shared_ptr<StreamingSharedState> state)
        : state_(std::move(state)) {
        reset();
    }

    void reset() override {
        std::lock_guard<std::mutex> lock{state_->mutex};
        stream_ = state_->recognizer.CreateStream();
        if (stream_.Get() == nullptr) {
            throw Error{ErrorCode::ProcessingError,
                        "Failed to create sherpa-onnx streaming ASR stream"};
        }
        finalized_ = false;
    }

    void accept(AudioView audio) override {
        if (finalized_) {
            throw Error{ErrorCode::InvalidState,
                        "Streaming ASR session must be reset after finalize"};
        }
        sherpa_detail::validateMonoAudio(
            audio, state_->options.features.sample_rate_hz, "streaming ASR");
        if (audio.sampleCount() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa streaming ASR chunk exceeds backend range"};
        }

        std::lock_guard<std::mutex> lock{state_->mutex};
        if (audio.sampleCount() > 0U) {
            stream_.AcceptWaveform(
                static_cast<std::int32_t>(audio.format().sample_rate_hz),
                audio.data(),
                static_cast<std::int32_t>(audio.sampleCount()));
        }
        while (state_->recognizer.IsReady(&stream_)) {
            state_->recognizer.Decode(&stream_);
        }
    }

    Transcript partial() const override {
        std::lock_guard<std::mutex> lock{state_->mutex};
        return sherpa_detail::transcriptFromOnline(
            state_->recognizer.GetResult(&stream_));
    }

    bool endpointDetected() const override {
        std::lock_guard<std::mutex> lock{state_->mutex};
        return state_->recognizer.IsEndpoint(&stream_);
    }

    Transcript finalize() override {
        if (!finalized_) {
            std::lock_guard<std::mutex> lock{state_->mutex};
            stream_.InputFinished();
            while (state_->recognizer.IsReady(&stream_)) {
                state_->recognizer.Decode(&stream_);
            }
            finalized_ = true;
            return sherpa_detail::transcriptFromOnline(
                state_->recognizer.GetResult(&stream_));
        }
        return partial();
    }

private:
    std::shared_ptr<StreamingSharedState> state_;
    sherpa_onnx::cxx::OnlineStream stream_{nullptr};
    bool finalized_{false};
};

ExecutionCapabilities executionCapabilities(const SherpaRuntimeOptions& runtime) {
    ExecutionCapabilities capabilities{};
    capabilities.device_classes = {runtime.execution.device_class};
    capabilities.providers = {sherpa_detail::providerFor(runtime)};
    return capabilities;
}

}  // namespace

class SherpaStreamingAsr::Impl {
public:
    explicit Impl(SherpaStreamingAsrOptions options)
        : state_(std::make_shared<StreamingSharedState>(std::move(options))) {}

    std::shared_ptr<StreamingSharedState> state_;
};

SherpaStreamingAsr::SherpaStreamingAsr(SherpaStreamingAsrOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaStreamingAsr::~SherpaStreamingAsr() = default;
SherpaStreamingAsr::SherpaStreamingAsr(SherpaStreamingAsr&&) noexcept = default;
SherpaStreamingAsr& SherpaStreamingAsr::operator=(SherpaStreamingAsr&&) noexcept = default;

BackendInfo SherpaStreamingAsr::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

AsrCapabilities SherpaStreamingAsr::capabilities() const {
    AsrCapabilities capabilities{};
    capabilities.token_timestamps = true;
    capabilities.word_timestamps = false;
    capabilities.language_identification = false;
    capabilities.partial_results = true;
    capabilities.endpoint_detection = true;
    capabilities.audio =
        sherpa_detail::monoRequirements(impl_->state_->options.features.sample_rate_hz);
    capabilities.execution =
        executionCapabilities(impl_->state_->options.runtime);
    return capabilities;
}

std::unique_ptr<IStreamingAsrSession> SherpaStreamingAsr::createSession() const {
    return std::make_unique<SherpaStreamingSession>(impl_->state_);
}

const SherpaStreamingAsrOptions& SherpaStreamingAsr::options() const noexcept {
    return impl_->state_->options;
}

}  // namespace audition
