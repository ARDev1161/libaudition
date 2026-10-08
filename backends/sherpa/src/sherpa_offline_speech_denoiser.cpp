#include <audition/backends/sherpa/offline_speech_denoiser.hpp>

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

class OfflineDenoiserSession final : public INoiseSuppressorSession {
public:
    OfflineDenoiserSession(
        sherpa_onnx::cxx::OfflineSpeechDenoiserConfig config,
        AudioFormat format)
        : denoiser_(sherpa_onnx::cxx::OfflineSpeechDenoiser::Create(config)),
          format_(format) {
        if (denoiser_.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx offline speech denoiser"};
        }
        const auto native_rate = denoiser_.GetSampleRate();
        if (native_rate <= 0 ||
            static_cast<std::uint32_t>(native_rate) !=
                format_.sample_rate_hz) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "Sherpa offline denoiser session sample rate does not match the model"};
        }
    }

    void reset() override {
        flushed_ = false;
        next_output_time_.reset();
        last_sequence_number_ = 0U;
    }

    AudioBuffer process(AudioView input) override {
        if (flushed_) {
            throw Error{ErrorCode::InvalidState,
                        "Offline denoiser session must be reset after flush"};
        }

        sherpa_detail::validateDenoiserInput(
            input, format_, "offline speech denoiser");
        auto output = denoiser_.Run(
            input.data(),
            static_cast<std::int32_t>(input.sampleCount()),
            static_cast<std::int32_t>(input.format().sample_rate_hz));

        auto result = sherpa_detail::denoisedAudioBuffer(
            std::move(output), format_, input.captureTime(),
            input.sequenceNumber(), "offline speech denoiser");
        next_output_time_ =
            result.captureTime().advancedBy(result.duration());
        last_sequence_number_ = input.sequenceNumber();
        return result;
    }

    AudioBuffer flush() override {
        flushed_ = true;
        return AudioBuffer{{}, format_,
                           next_output_time_.value_or(Timestamp{}),
                           last_sequence_number_};
    }

private:
    sherpa_onnx::cxx::OfflineSpeechDenoiser denoiser_;
    AudioFormat format_{};
    bool flushed_{false};
    std::optional<Timestamp> next_output_time_{};
    std::uint64_t last_sequence_number_{0U};
};

}  // namespace

class SherpaOfflineSpeechDenoiser::Impl {
public:
    explicit Impl(SherpaSpeechDenoiserOptions options)
        : options_(std::move(options)),
          config_(sherpa_detail::makeOfflineSpeechDenoiserConfig(options_)) {
        auto probe =
            sherpa_onnx::cxx::OfflineSpeechDenoiser::Create(config_);
        if (probe.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx offline speech denoiser"};
        }

        const auto native_rate = probe.GetSampleRate();
        if (native_rate <= 0) {
            throw Error{ErrorCode::ModelLoadError,
                        "Sherpa offline speech denoiser reported an invalid sample rate"};
        }
        sample_rate_hz_ = static_cast<std::uint32_t>(native_rate);
    }

    SherpaSpeechDenoiserOptions options_{};
    sherpa_onnx::cxx::OfflineSpeechDenoiserConfig config_{};
    std::uint32_t sample_rate_hz_{0U};
};

SherpaOfflineSpeechDenoiser::SherpaOfflineSpeechDenoiser(
    SherpaSpeechDenoiserOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaOfflineSpeechDenoiser::~SherpaOfflineSpeechDenoiser() = default;
SherpaOfflineSpeechDenoiser::SherpaOfflineSpeechDenoiser(
    SherpaOfflineSpeechDenoiser&&) noexcept = default;
SherpaOfflineSpeechDenoiser& SherpaOfflineSpeechDenoiser::operator=(
    SherpaOfflineSpeechDenoiser&&) noexcept = default;

BackendInfo SherpaOfflineSpeechDenoiser::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

NoiseSuppressorCapabilities SherpaOfflineSpeechDenoiser::capabilities() const {
    NoiseSuppressorCapabilities capabilities{};
    capabilities.streaming = false;
    capabilities.audio =
        sherpa_detail::monoRequirements(impl_->sample_rate_hz_);
    capabilities.execution.device_classes = {
        impl_->options_.runtime.execution.device_class};
    capabilities.execution.providers = {
        sherpa_detail::providerFor(impl_->options_.runtime)};
    return capabilities;
}

std::unique_ptr<INoiseSuppressorSession>
SherpaOfflineSpeechDenoiser::createSession(
    const AudioFormat& format) const {
    sherpa_detail::validateDenoiserFormat(
        format, impl_->sample_rate_hz_, "offline speech denoiser");
    return std::make_unique<OfflineDenoiserSession>(
        impl_->config_, format);
}

const SherpaSpeechDenoiserOptions&
SherpaOfflineSpeechDenoiser::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
