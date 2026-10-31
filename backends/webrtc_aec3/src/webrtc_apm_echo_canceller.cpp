#include <audition/backends/webrtc_aec3/webrtc_aec3_echo_canceller.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <modules/audio_processing/include/audio_processing.h>
#include <audition/core/error.hpp>

namespace audition {
namespace {

bool validRate(std::uint32_t rate) noexcept {
    return rate == 16000U || rate == 32000U || rate == 48000U;
}

void validateFormats(const AudioFormat& capture, const AudioFormat& reference) {
    if (!capture.valid() || !reference.valid()) {
        throw Error{ErrorCode::ConfigurationError,
                    "WebRTC APM requires valid capture/reference formats"};
    }
    if (capture.sample_rate_hz != reference.sample_rate_hz) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "WebRTC APM capture and reference sample rates must match"};
    }
    if (!validRate(capture.sample_rate_hz)) {
        throw Error{ErrorCode::UnsupportedFormat,
                    "WebRTC APM AEC3 supports 16, 32 or 48 kHz"};
    }
}

void validateFrame(AudioView view, const AudioFormat& expected,
                   const char* role) {
    if (view.format().sample_rate_hz != expected.sample_rate_hz ||
        view.format().channel_count != expected.channel_count ||
        view.format().layout != expected.layout ||
        view.frameCount() != static_cast<std::size_t>(expected.sample_rate_hz / 100U)) {
        throw Error{ErrorCode::UnsupportedFormat,
                    std::string{"WebRTC APM "} + role +
                    " must be a 10-ms frame in session format"};
    }
}

std::vector<std::vector<float>> unpack(AudioView input) {
    std::vector<std::vector<float>> planar(input.format().channel_count);
    for (std::size_t ch = 0; ch < planar.size(); ++ch) {
        const auto samples = input.channel(ch);
        planar[ch].resize(samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i)
            planar[ch][i] = samples[i];
    }
    return planar;
}

std::vector<const float*> inputPointers(
    const std::vector<std::vector<float>>& planar) {
    std::vector<const float*> pointers;
    pointers.reserve(planar.size());
    for (const auto& ch : planar)
        pointers.push_back(ch.data());
    return pointers;
}

std::vector<float*> outputPointers(std::vector<std::vector<float>>& planar) {
    std::vector<float*> pointers;
    pointers.reserve(planar.size());
    for (auto& ch : planar)
        pointers.push_back(ch.data());
    return pointers;
}

std::vector<float> pack(const std::vector<std::vector<float>>& planar,
                        AudioLayout layout) {
    if (planar.empty()) return {};
    const auto frames = planar.front().size();
    std::vector<float> output(frames * planar.size());
    for (std::size_t ch = 0; ch < planar.size(); ++ch) {
        for (std::size_t i = 0; i < frames; ++i) {
            const auto index = layout == AudioLayout::Interleaved
                ? i * planar.size() + ch : ch * frames + i;
            output[index] = std::clamp(planar[ch][i], -1.0F, 1.0F);
        }
    }
    return output;
}

void check(int code, const char* step) {
    if (code != webrtc::AudioProcessing::kNoError) {
        throw Error{ErrorCode::ProcessingError,
                    std::string{"WebRTC APM "} + step +
                    " returned " + std::to_string(code)};
    }
}

// One independent APM instance for each capture/reference stream pair.
class ApmEchoSession final : public IEchoCancellerSession {
public:
    ApmEchoSession(AudioFormat capture, AudioFormat reference,
                   WebRtcAec3Options options)
        : capture_(capture), reference_(reference), options_(options) {
        validateFormats(capture_, reference_);
        // APM 2.x does not expose the standalone extraction's two AEC3
        // tuning fields through the public APM configuration. Reject such
        // requests rather than silently discarding them.
        if (!std::isfinite(options_.filter_initial_state_seconds) ||
            options_.filter_initial_state_seconds <= 0.0 ||
            options_.filter_initial_state_seconds != 0.5 ||
            options_.conservative_initial_phase) {
            throw Error{ErrorCode::ConfigurationError,
                        "Custom AEC3 filter initialization settings are not "
                        "exposed by the unified WebRTC APM API"};
        }
        rebuild();
    }

    void reset() override { rebuild(); }

    void acceptReference(AudioView playback) override {
        validateFrame(playback, reference_, "render");
        auto samples = unpack(playback);
        auto in = inputPointers(samples);
        auto out = outputPointers(samples);
        const webrtc::StreamConfig format{
            static_cast<int>(reference_.sample_rate_hz),
            reference_.channel_count};
        check(engine_->ProcessReverseStream(in.data(), format, format, out.data()),
              "ProcessReverseStream");
    }

    void setStreamDelay(Duration delay) override {
        if (delay.nanoseconds() < 0) {
            throw Error{ErrorCode::InvalidArgument,
                        "AEC3 stream delay must be nonnegative"};
        }
        const auto value = delay.nanoseconds() / 1'000'000;
        if (value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "AEC3 stream delay overflows milliseconds"};
        }
        delay_ms_ = static_cast<int>(value);
        check(engine_->set_stream_delay_ms(*delay_ms_),
              "set_stream_delay_ms");
    }

    void notifyEchoPathGainChange() override {
        // APM owns its echo-path-change analysis. The public API exposes no
        // direct equivalent to EchoControl::ProcessCapture(level_change).
    }

    AudioBuffer process(AudioView captured) override {
        validateFrame(captured, capture_, "capture");
        // APM requires a stream-delay value for each capture block when
        // echo cancellation is enabled, even if the application uses zero.
        check(engine_->set_stream_delay_ms(delay_ms_.value_or(0)),
              "set_stream_delay_ms before capture");
        auto samples = unpack(captured);
        auto in = inputPointers(samples);
        auto out = outputPointers(samples);
        const webrtc::StreamConfig format{
            static_cast<int>(capture_.sample_rate_hz),
            capture_.channel_count};
        check(engine_->ProcessStream(in.data(), format, format, out.data()),
              "ProcessStream");
        return AudioBuffer{
            pack(samples, capture_.layout),
            capture_,
            captured.captureTime(),
            captured.sequenceNumber()};
    }

    EchoCancellationMetrics metrics() const override {
        const auto stats = engine_->GetStatistics();
        EchoCancellationMetrics result{};
        if (stats.echo_return_loss &&
            std::isfinite(*stats.echo_return_loss))
            result.echo_return_loss_db = *stats.echo_return_loss;
        if (stats.echo_return_loss_enhancement &&
            std::isfinite(*stats.echo_return_loss_enhancement))
            result.echo_return_loss_enhancement_db =
                *stats.echo_return_loss_enhancement;
        if (stats.delay_ms && *stats.delay_ms >= 0)
            result.estimated_delay =
                Duration{static_cast<std::int64_t>(*stats.delay_ms) * 1'000'000};
        return result;
    }

private:
    void rebuild() {
        webrtc::AudioProcessing::Config config{};
        config.echo_canceller.enabled = true;
        config.pipeline.multi_channel_capture = capture_.channel_count > 1U;
        config.pipeline.multi_channel_render = reference_.channel_count > 1U;
        config.gain_controller1.enabled = false;
        config.gain_controller2.enabled = false;
        config.noise_suppression.enabled = false;

        auto engine = webrtc::AudioProcessingBuilder()
            .SetConfig(config).Create();
        if (!engine) {
            throw Error{ErrorCode::ConfigurationError,
                        "WebRTC APM could not initialize echo cancellation"};
        }
        const webrtc::ProcessingConfig streams{
            webrtc::StreamConfig(
                static_cast<int>(capture_.sample_rate_hz),
                capture_.channel_count),
            webrtc::StreamConfig(
                static_cast<int>(capture_.sample_rate_hz),
                capture_.channel_count),
            webrtc::StreamConfig(
                static_cast<int>(reference_.sample_rate_hz),
                reference_.channel_count),
            webrtc::StreamConfig(
                static_cast<int>(reference_.sample_rate_hz),
                reference_.channel_count)};
        check(engine->Initialize(streams), "Initialize");
        if (delay_ms_)
            check(engine->set_stream_delay_ms(*delay_ms_),
                  "set_stream_delay_ms after reset");
        engine_ = std::move(engine);
    }

    AudioFormat capture_{};
    AudioFormat reference_{};
    WebRtcAec3Options options_{};
    decltype(webrtc::AudioProcessingBuilder{}.Create()) engine_{};
    std::optional<int> delay_ms_{};
};
} // namespace

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
    // Preserve the public backend identifier across the APM migration.
    return {"webrtc-aec3", "2.x"};
}

std::unique_ptr<IEchoCancellerSession> WebRtcAec3EchoCanceller::createSession(
    const AudioFormat& capture_format, const AudioFormat& reference_format) const {
    return std::make_unique<ApmEchoSession>(
        capture_format, reference_format, impl_->options_);
}

const WebRtcAec3Options& WebRtcAec3EchoCanceller::options() const noexcept {
    return impl_->options_;
}
} // namespace audition
