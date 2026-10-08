#include <audition/backends/webrtc_aec3/webrtc_aec3_echo_canceller.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "api/echo_canceller3_config.h"
#include "api/echo_canceller3_factory.h"
#include "api/echo_control.h"
#include "api/environment.h"
#include "audio_processing/audio_buffer.h"

#include <audition/core/error.hpp>

namespace audition {
namespace {

bool supportedSampleRate(std::uint32_t sample_rate_hz) noexcept {
    return sample_rate_hz == 16000U || sample_rate_hz == 32000U ||
           sample_rate_hz == 48000U;
}

void validateFormats(const AudioFormat& capture, const AudioFormat& reference) {
    if (!capture.valid() || !reference.valid()) {
        throw Error{ErrorCode::ConfigurationError,
                    "WebRTC AEC3 requires valid capture/reference formats"};
    }
    if (capture.sample_rate_hz != reference.sample_rate_hz) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "WebRTC AEC3 capture and reference sample rates must match"};
    }
    if (!supportedSampleRate(capture.sample_rate_hz)) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "WebRTC AEC3 supports 16, 32, or 48 kHz"};
    }
}

float toAecScale(float normalized) noexcept {
    const auto clamped = std::clamp(normalized, -1.0F, 1.0F);
    return clamped >= 0.0F ? clamped * 32767.0F : clamped * 32768.0F;
}

float fromAecScale(float sample) noexcept {
    const auto clamped = std::clamp(sample, -32768.0F, 32767.0F);
    return clamped >= 0.0F ? clamped / 32767.0F : clamped / 32768.0F;
}

void fillBuffer(webrtc::AudioBuffer& destination, AudioView source) {
    auto* const* channels = destination.channels();
    for (std::size_t channel = 0; channel < source.format().channel_count; ++channel) {
        const auto input = source.channel(channel);
        for (std::size_t frame = 0; frame < input.size(); ++frame) {
            channels[channel][frame] = toAecScale(input[frame]);
        }
    }
}

std::vector<float> extractBuffer(const webrtc::AudioBuffer& source,
                                 std::size_t frames,
                                 std::size_t channels,
                                 AudioLayout layout) {
    std::vector<float> output(frames * channels, 0.0F);
    const auto* const* source_channels = source.channels_const();
    for (std::size_t channel = 0; channel < channels; ++channel) {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto index = layout == AudioLayout::Interleaved
                                   ? frame * channels + channel
                                   : channel * frames + frame;
            output[index] = fromAecScale(source_channels[channel][frame]);
        }
    }
    return output;
}

class WebRtcAec3Session final : public IEchoCancellerSession {
public:
    WebRtcAec3Session(AudioFormat capture_format,
                      AudioFormat reference_format,
                      WebRtcAec3Options options)
        : capture_format_(capture_format),
          reference_format_(reference_format),
          options_(options) {
        validateFormats(capture_format_, reference_format_);
        if (!std::isfinite(options_.filter_initial_state_seconds) ||
            options_.filter_initial_state_seconds <= 0.0) {
            throw Error{ErrorCode::ConfigurationError,
                        "AEC3 filter_initial_state_seconds must be positive"};
        }
        rebuild();
    }

    void reset() override {
        rebuild();
        echo_path_change_ = false;
    }

    void acceptReference(AudioView playback) override {
        validateFrame(playback, reference_format_, "reference");
        fillBuffer(*render_buffer_, playback);
        echo_control_->AnalyzeRender(render_buffer_.get());
    }

    void setStreamDelay(Duration delay) override {
        if (delay.nanoseconds() < 0) {
            throw Error{ErrorCode::InvalidArgument,
                        "AEC stream delay must be non-negative"};
        }
        const auto milliseconds =
            delay.nanoseconds() / 1'000'000;
        if (milliseconds > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "AEC stream delay exceeds backend range"};
        }
        delay_ms_ = static_cast<int>(milliseconds);
        echo_control_->SetAudioBufferDelay(*delay_ms_);
    }

    void notifyEchoPathChange() override {
        echo_path_change_ = true;
    }

    AudioBuffer process(AudioView captured) override {
        validateFrame(captured, capture_format_, "capture");
        fillBuffer(*capture_buffer_, captured);
        echo_control_->AnalyzeCapture(capture_buffer_.get());
        echo_control_->ProcessCapture(capture_buffer_.get(), echo_path_change_);
        echo_path_change_ = false;

        return AudioBuffer{
            extractBuffer(*capture_buffer_, captured.frameCount(),
                          capture_format_.channel_count, capture_format_.layout),
            capture_format_,
            captured.captureTime(),
            captured.sequenceNumber()};
    }

    EchoCancellationMetrics metrics() const override {
        const auto native = echo_control_->GetMetrics();
        EchoCancellationMetrics result{};
        if (std::isfinite(native.echo_return_loss)) {
            result.echo_return_loss_db = native.echo_return_loss;
        }
        if (std::isfinite(native.echo_return_loss_enhancement)) {
            result.echo_return_loss_enhancement_db =
                native.echo_return_loss_enhancement;
        }
        if (native.delay_ms >= 0) {
            result.estimated_delay =
                Duration{static_cast<std::int64_t>(native.delay_ms) * 1'000'000};
        }
        return result;
    }

private:
    void rebuild() {
        environment_ = std::make_unique<webrtc::Environment>();

        webrtc::EchoCanceller3Config config{};
        config.filter.initial_state_seconds =
            static_cast<float>(options_.filter_initial_state_seconds);
        config.filter.conservative_initial_phase =
            options_.conservative_initial_phase;

        factory_ = std::make_unique<webrtc::EchoCanceller3Factory>(config);
        echo_control_ = factory_->Create(
            *environment_,
            static_cast<int>(capture_format_.sample_rate_hz),
            static_cast<int>(reference_format_.channel_count),
            static_cast<int>(capture_format_.channel_count));

        render_buffer_ = std::make_unique<webrtc::AudioBuffer>(
            reference_format_.sample_rate_hz,
            reference_format_.channel_count,
            reference_format_.sample_rate_hz,
            reference_format_.channel_count,
            reference_format_.sample_rate_hz,
            reference_format_.channel_count);

        capture_buffer_ = std::make_unique<webrtc::AudioBuffer>(
            capture_format_.sample_rate_hz,
            capture_format_.channel_count,
            capture_format_.sample_rate_hz,
            capture_format_.channel_count,
            capture_format_.sample_rate_hz,
            capture_format_.channel_count);

        if (delay_ms_.has_value()) {
            echo_control_->SetAudioBufferDelay(*delay_ms_);
        }
    }

    void validateFrame(AudioView audio, const AudioFormat& expected,
                       const char* role) const {
        if (audio.format().sample_rate_hz != expected.sample_rate_hz ||
            audio.format().channel_count != expected.channel_count ||
            audio.format().layout != expected.layout) {
            throw Error{ErrorCode::UnsupportedFormat,
                        std::string{"AEC3 "} + role +
                            " frame does not match session format"};
        }
        const auto expected_frames =
            static_cast<std::size_t>(expected.sample_rate_hz / 100U);
        if (audio.frameCount() != expected_frames) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "WebRTC AEC3 requires exactly 10 ms frames"};
        }
    }

    AudioFormat capture_format_{};
    AudioFormat reference_format_{};
    WebRtcAec3Options options_{};
    std::unique_ptr<webrtc::Environment> environment_{};
    std::unique_ptr<webrtc::EchoCanceller3Factory> factory_{};
    std::unique_ptr<webrtc::EchoControl> echo_control_{};
    std::unique_ptr<webrtc::AudioBuffer> render_buffer_{};
    std::unique_ptr<webrtc::AudioBuffer> capture_buffer_{};
    std::optional<int> delay_ms_{};
    bool echo_path_change_{false};
};

}  // namespace

class WebRtcAec3EchoCanceller::Impl {
public:
    explicit Impl(WebRtcAec3Options options) : options_(options) {}
    WebRtcAec3Options options_{};
};

WebRtcAec3EchoCanceller::WebRtcAec3EchoCanceller(WebRtcAec3Options options)
    : impl_(std::make_unique<Impl>(options)) {}
WebRtcAec3EchoCanceller::~WebRtcAec3EchoCanceller() = default;
WebRtcAec3EchoCanceller::WebRtcAec3EchoCanceller(WebRtcAec3EchoCanceller&&) noexcept = default;
WebRtcAec3EchoCanceller& WebRtcAec3EchoCanceller::operator=(
    WebRtcAec3EchoCanceller&&) noexcept = default;

BackendInfo WebRtcAec3EchoCanceller::backendInfo() const {
    return {"webrtc-aec3", "2cec2f52"};
}

std::unique_ptr<IEchoCancellerSession> WebRtcAec3EchoCanceller::createSession(
    const AudioFormat& capture_format,
    const AudioFormat& reference_format) const {
    return std::make_unique<WebRtcAec3Session>(
        capture_format, reference_format, impl_->options_);
}

const WebRtcAec3Options& WebRtcAec3EchoCanceller::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
