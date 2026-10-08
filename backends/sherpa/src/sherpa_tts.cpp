#include <audition/backends/sherpa/sherpa_tts.hpp>

#if defined(LIBAUDITION_SHERPA_TTS_ENABLED)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"
#include "detail/sherpa_tts.hpp"

namespace audition {
namespace {

bool languageSupported(const std::vector<std::string>& supported,
                       const std::string& requested) {
    return std::find(supported.begin(), supported.end(), requested) !=
           supported.end();
}

}  // namespace

class SherpaTts::Impl {
public:
    explicit Impl(SherpaTtsOptions options)
        : options_(std::move(options)),
          tts_(sherpa_onnx::cxx::OfflineTts::Create(
              sherpa_detail::makeTtsConfig(options_))) {
        if (tts_.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx TTS engine"};
        }

        sample_rate_hz_ = tts_.SampleRate();
        num_speakers_ = tts_.NumSpeakers();
        if (sample_rate_hz_ <= 0 || num_speakers_ < 0) {
            throw Error{ErrorCode::ModelLoadError,
                        "Sherpa TTS reported invalid model metadata"};
        }

        if (num_speakers_ == 0) {
            if (options_.speaker_id != 0) {
                throw Error{ErrorCode::ConfigurationError,
                            "Single-speaker Sherpa TTS models require speaker_id 0"};
            }
        } else if (options_.speaker_id >= num_speakers_) {
            throw Error{ErrorCode::ConfigurationError,
                        "Sherpa TTS speaker_id exceeds the model speaker count"};
        }
    }

    SherpaTtsOptions options_{};
    mutable sherpa_onnx::cxx::OfflineTts tts_;
    std::int32_t sample_rate_hz_{0};
    std::int32_t num_speakers_{0};
    mutable std::mutex mutex_{};
};

SherpaTts::SherpaTts(SherpaTtsOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaTts::~SherpaTts() = default;
SherpaTts::SherpaTts(SherpaTts&&) noexcept = default;
SherpaTts& SherpaTts::operator=(SherpaTts&&) noexcept = default;

BackendInfo SherpaTts::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

TtsCapabilities SherpaTts::capabilities() const {
    TtsCapabilities capabilities{};
    capabilities.streaming = false;
    capabilities.voice_cloning = false;
    capabilities.style_control = false;
    capabilities.languages = impl_->options_.languages;
    capabilities.execution.device_classes = {
        impl_->options_.runtime.execution.device_class};
    capabilities.execution.providers = {
        sherpa_detail::providerFor(impl_->options_.runtime)};
    return capabilities;
}

AudioBuffer SherpaTts::synthesize(
    const SpeechSynthesisRequest& request) const {
    if (request.text.empty()) {
        throw Error{ErrorCode::InvalidArgument,
                    "Sherpa TTS requires non-empty text"};
    }
    if (request.voice_reference.has_value()) {
        throw Error{ErrorCode::UnsupportedCapability,
                    "This Sherpa TTS slice does not support voice references"};
    }
    if (!std::isfinite(request.speed) || request.speed <= 0.0 ||
        request.speed >
            static_cast<double>(std::numeric_limits<float>::max())) {
        throw Error{ErrorCode::InvalidArgument,
                    "Sherpa TTS speed must be finite and positive"};
    }

    if (!request.language.empty()) {
        if (impl_->options_.languages.empty() ||
            !languageSupported(impl_->options_.languages, request.language)) {
            throw Error{ErrorCode::UnsupportedCapability,
                        "Requested language is not declared by this Sherpa TTS model"};
        }
    }

    sherpa_onnx::cxx::GenerationConfig generation{};
    generation.silence_scale = impl_->options_.silence_scale;
    generation.speed = static_cast<float>(request.speed);
    generation.sid = impl_->options_.speaker_id;

    std::lock_guard<std::mutex> lock{impl_->mutex_};
    auto output = impl_->tts_.Generate(request.text, generation);
    if (output.sample_rate != impl_->sample_rate_hz_ ||
        output.sample_rate <= 0) {
        throw Error{ErrorCode::ProcessingError,
                    "Sherpa TTS returned an unexpected sample rate"};
    }
    if (output.samples.empty()) {
        throw Error{ErrorCode::ProcessingError,
                    "Sherpa TTS returned empty audio"};
    }

    return AudioBuffer{
        std::move(output.samples),
        AudioFormat{static_cast<std::uint32_t>(output.sample_rate), 1U,
                    AudioLayout::Interleaved},
        Timestamp{}};
}

const SherpaTtsOptions& SherpaTts::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition

#endif
