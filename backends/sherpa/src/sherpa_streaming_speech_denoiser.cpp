#include <audition/backends/sherpa/streaming_speech_denoiser.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"
#include "detail/sherpa_speech_denoiser.hpp"

namespace audition {
namespace {

class StreamingDenoiserSession final : public INoiseSuppressorSession {
public:
    StreamingDenoiserSession(
        sherpa_onnx::cxx::OnlineSpeechDenoiserConfig config,
        AudioFormat format)
        : denoiser_(sherpa_onnx::cxx::OnlineSpeechDenoiser::Create(config)),
          format_(format) {
        if (denoiser_.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx streaming speech denoiser"};
        }

        const auto native_rate = denoiser_.GetSampleRate();
        if (native_rate <= 0 ||
            static_cast<std::uint32_t>(native_rate) !=
                format_.sample_rate_hz) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "Sherpa streaming denoiser session sample rate does not match the model"};
        }
    }

    void reset() override {
        denoiser_.Reset();
        flushed_ = false;
        next_output_time_.reset();
        last_sequence_number_ = 0U;
    }

    AudioBuffer process(AudioView input) override {
        if (flushed_) {
            throw Error{ErrorCode::InvalidState,
                        "Streaming denoiser session must be reset after flush"};
        }

        sherpa_detail::validateDenoiserInput(
            input, format_, "streaming speech denoiser");
        if (!next_output_time_.has_value()) {
            next_output_time_ = input.captureTime();
        }
        last_sequence_number_ = input.sequenceNumber();

        auto output = denoiser_.Run(
            input.data(),
            static_cast<std::int32_t>(input.sampleCount()),
            static_cast<std::int32_t>(input.format().sample_rate_hz));

        auto result = sherpa_detail::denoisedAudioBuffer(
            std::move(output), format_, *next_output_time_,
            input.sequenceNumber(), "streaming speech denoiser");
        next_output_time_ =
            result.captureTime().advancedBy(result.duration());
        return result;
    }

    AudioBuffer flush() override {
        if (flushed_) {
            return emptyOutput();
        }
        flushed_ = true;

        auto output = denoiser_.Flush();
        const auto timestamp =
            next_output_time_.value_or(Timestamp{});
        auto result = sherpa_detail::denoisedAudioBuffer(
            std::move(output), format_, timestamp,
            last_sequence_number_, "streaming speech denoiser");
        next_output_time_ =
            result.captureTime().advancedBy(result.duration());
        return result;
    }

private:
    [[nodiscard]] AudioBuffer emptyOutput() const {
        return AudioBuffer{{}, format_,
                           next_output_time_.value_or(Timestamp{}),
                           last_sequence_number_};
    }

    sherpa_onnx::cxx::OnlineSpeechDenoiser denoiser_;
    AudioFormat format_{};
    bool flushed_{false};
    std::optional<Timestamp> next_output_time_{};
    std::uint64_t last_sequence_number_{0U};
};

}  // namespace

class SherpaStreamingSpeechDenoiser::Impl {
public:
    explicit Impl(SherpaSpeechDenoiserOptions options)
        : options_(std::move(options)),
          config_(sherpa_detail::makeStreamingSpeechDenoiserConfig(options_)) {
        auto probe =
            sherpa_onnx::cxx::OnlineSpeechDenoiser::Create(config_);
        if (probe.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx streaming speech denoiser"};
        }

        const auto native_rate = probe.GetSampleRate();
        const auto native_frame_shift = probe.GetFrameShiftInSamples();
        if (native_rate <= 0 || native_frame_shift <= 0) {
            throw Error{ErrorCode::ModelLoadError,
                        "Sherpa streaming speech denoiser reported invalid model metadata"};
        }

        sample_rate_hz_ = static_cast<std::uint32_t>(native_rate);
        preferred_frame_count_ =
            static_cast<std::size_t>(native_frame_shift);
    }

    SherpaSpeechDenoiserOptions options_{};
    sherpa_onnx::cxx::OnlineSpeechDenoiserConfig config_{};
    std::uint32_t sample_rate_hz_{0U};
    std::size_t preferred_frame_count_{0U};
};

SherpaStreamingSpeechDenoiser::SherpaStreamingSpeechDenoiser(
    SherpaSpeechDenoiserOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaStreamingSpeechDenoiser::~SherpaStreamingSpeechDenoiser() = default;
SherpaStreamingSpeechDenoiser::SherpaStreamingSpeechDenoiser(
    SherpaStreamingSpeechDenoiser&&) noexcept = default;
SherpaStreamingSpeechDenoiser& SherpaStreamingSpeechDenoiser::operator=(
    SherpaStreamingSpeechDenoiser&&) noexcept = default;

BackendInfo SherpaStreamingSpeechDenoiser::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

NoiseSuppressorCapabilities SherpaStreamingSpeechDenoiser::capabilities() const {
    NoiseSuppressorCapabilities capabilities{};
    capabilities.streaming = true;
    capabilities.audio =
        sherpa_detail::monoRequirements(impl_->sample_rate_hz_);
    capabilities.audio.preferred_frame_count =
        impl_->preferred_frame_count_;
    capabilities.execution.device_classes = {
        impl_->options_.runtime.execution.device_class};
    capabilities.execution.providers = {
        sherpa_detail::providerFor(impl_->options_.runtime)};
    return capabilities;
}

std::unique_ptr<INoiseSuppressorSession>
SherpaStreamingSpeechDenoiser::createSession(
    const AudioFormat& format) const {
    sherpa_detail::validateDenoiserFormat(
        format, impl_->sample_rate_hz_, "streaming speech denoiser");
    return std::make_unique<StreamingDenoiserSession>(
        impl_->config_, format);
}

const SherpaSpeechDenoiserOptions&
SherpaStreamingSpeechDenoiser::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
