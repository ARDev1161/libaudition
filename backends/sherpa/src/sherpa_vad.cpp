#include <audition/backends/sherpa/vad.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"

namespace audition {
namespace {

class SherpaVadSession final : public IVadSession {
public:
    SherpaVadSession(const SherpaVadOptions& options,
                     const sherpa_onnx::cxx::VadModelConfig& config)
        : sample_rate_hz_(options.sample_rate_hz),
          detector_(sherpa_onnx::cxx::VoiceActivityDetector::Create(
              config, options.buffer_size_seconds)) {
        if (detector_.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx VAD"};
        }
    }

    void reset() override {
        detector_.Reset();
        detector_.Clear();
    }

    VadResult process(AudioView audio) override {
        sherpa_detail::validateMonoAudio(audio, sample_rate_hz_, "VAD");
        if (audio.sampleCount() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa VAD input chunk exceeds backend range"};
        }

        if (audio.sampleCount() > 0U) {
            detector_.AcceptWaveform(
                audio.data(), static_cast<std::int32_t>(audio.sampleCount()));
        }

        VadResult result{};
        result.speech_active = detector_.IsDetected();
        result.speech_probability.reset();

        // libaudition has a separate segmentation abstraction. Drain completed
        // native VAD segments here so a pure IVadSession does not accumulate
        // an unbounded queue that the generic interface cannot consume.
        while (!detector_.IsEmpty()) {
            detector_.Pop();
        }
        return result;
    }

private:
    std::uint32_t sample_rate_hz_{16000U};
    sherpa_onnx::cxx::VoiceActivityDetector detector_;
};

std::size_t preferredWindow(const SherpaVadOptions& options) {
    return std::visit(
        [](const auto& model) {
            return static_cast<std::size_t>(model.window_size);
        },
        options.model);
}

}  // namespace

class SherpaVad::Impl {
public:
    explicit Impl(SherpaVadOptions options)
        : options_(std::move(options)), config_(sherpa_detail::makeVadConfig(options_)) {}

    SherpaVadOptions options_{};
    sherpa_onnx::cxx::VadModelConfig config_{};
};

SherpaVad::SherpaVad(SherpaVadOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaVad::~SherpaVad() = default;
SherpaVad::SherpaVad(SherpaVad&&) noexcept = default;
SherpaVad& SherpaVad::operator=(SherpaVad&&) noexcept = default;

BackendInfo SherpaVad::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

AudioRequirements SherpaVad::audioRequirements() const {
    auto requirements = sherpa_detail::monoRequirements(impl_->options_.sample_rate_hz);
    requirements.preferred_frame_count = preferredWindow(impl_->options_);
    return requirements;
}

std::unique_ptr<IVadSession> SherpaVad::createSession() const {
    return std::make_unique<SherpaVadSession>(impl_->options_, impl_->config_);
}

const SherpaVadOptions& SherpaVad::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
