#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#if defined(LIBAUDITION_WITH_WEBRTC_AGC2) && LIBAUDITION_WITH_WEBRTC_AGC2
// The fetched AGC2 extraction and AEC3 define incompatible webrtc:: types.
// Compile the fetched AGC2 sources in a private namespace to avoid ELF/ODR
// collisions when both libraries are loaded into the Qt process.
#if defined(LIBAUDITION_AGC2_PRIVATE_NAMESPACE) && LIBAUDITION_AGC2_PRIVATE_NAMESPACE
#define webrtc libaudition_agc2_webrtc
#endif
#include <api/environment.h>
#include <audio_processing/audio_buffer.h>
#include <audio_processing/gain_controller2.h>
#if defined(LIBAUDITION_AGC2_PRIVATE_NAMESPACE) && LIBAUDITION_AGC2_PRIVATE_NAMESPACE
#undef webrtc
#endif
#endif
#if defined(LIBAUDITION_WITH_WEBRTC_APM) && LIBAUDITION_WITH_WEBRTC_APM
// Unified WebRTC APM 2.x public API (not standalone AGC2 internals).
#include <modules/audio_processing/include/audio_processing.h>
#endif
#if defined(LIBAUDITION_WITH_SPEEXDSP_AGC) && LIBAUDITION_WITH_SPEEXDSP_AGC
#include <speex/speex_preprocess.h>
#endif
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace audition::dsp {

// A stateful, replaceable speech gain processor; one instance per source.
class ISourceGain {
public:
    virtual ~ISourceGain() = default;
    virtual std::vector<float> process(const float* pcm, std::size_t count,
                                       std::uint32_t sample_rate) = 0;
    virtual double gainDb() const noexcept = 0;
    virtual void reset() noexcept = 0;
};

// Speech-oriented AGC on source-separated PCM. An energy gate prevents
// amplifying stationary noise; a short hold retains gain between syllables.
// The peak limiter is deliberately independent of the slow RMS controller.
class RmsSourceGain final : public ISourceGain {
public:
    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if (!sample_rate || (!pcm && count))
            throw std::invalid_argument("Invalid source gain PCM");
        std::vector<float> result(count);
        if (!count) return result;
        double energy = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite source gain sample");
            energy += static_cast<double>(pcm[i]) * pcm[i];
        }
        const double seconds = static_cast<double>(count) / sample_rate;
        const double rms = std::sqrt(energy / static_cast<double>(count));
        // Smooth power rather than dB values to avoid discontinuities.
        const double levelAlpha = -std::expm1(-seconds / 0.30);
        if (!initialized_) {
            smoothedPower_ = rms * rms;
            initialized_ = true;
        } else {
            smoothedPower_ += levelAlpha * (rms * rms - smoothedPower_);
        }
        const double smoothedDb = 10.0 * std::log10(std::max(1e-18, smoothedPower_));
        constexpr double kGateDb = -60.0;
        constexpr double kTargetDb = -26.0;
        constexpr double kMaxGainDb = 36.0;
        constexpr double kCeiling = 0.89;
        if (rms >= 0.001) holdSeconds_ = 0.35;
        else holdSeconds_ = std::max(0.0, holdSeconds_ - seconds);
        // On silence do not chase the decaying smoothed RMS; release only
        // after the hold expires to avoid pumping between spoken words.
        const double wanted = holdSeconds_ > 0.0 && smoothedDb > kGateDb
            ? std::clamp(kTargetDb - smoothedDb, 0.0, kMaxGainDb)
            : 0.0;
        const double tau = wanted > gain_db_ ? 0.25 : 0.60;
        const double alpha = -std::expm1(-seconds / tau);
        const double startGain = gain_db_;
        const double endGain = std::clamp(
            gain_db_ + alpha * (wanted - gain_db_), 0.0, kMaxGainDb);
        // A fast peak limiter prevents clipping without forcing the slow AGC
        // envelope to zero after a single transient.
        for (std::size_t i = 0; i < count; ++i) {
            const double fraction = static_cast<double>(i + 1) /
                static_cast<double>(count);
            const double db = startGain + (endGain - startGain) * fraction;
            const double candidate = static_cast<double>(pcm[i]) *
                std::pow(10.0, db / 20.0);
            result[i] = static_cast<float>(
                std::clamp(candidate, -kCeiling, kCeiling));
        }
        gain_db_ = endGain;
        return result;
    }
    double gainDb() const noexcept override { return gain_db_; }
    void reset() noexcept override {
        gain_db_ = 0.0;
        smoothedPower_ = 0.0;
        holdSeconds_ = 0.0;
        initialized_ = false;
    }
private:
    double gain_db_{0.0};
    double smoothedPower_{0.0};
    double holdSeconds_{0.0};
    bool initialized_{false};
};


#if defined(LIBAUDITION_WITH_SPEEXDSP_AGC) && LIBAUDITION_WITH_SPEEXDSP_AGC
// Adapts variable-sized ODAS chunks to Speex's 10-ms frames.
// Output includes one frame of initial latency; counts remain unchanged.
class SpeexSourceGain final : public ISourceGain {
public:
    ~SpeexSourceGain() override { reset(); }
    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if (!sample_rate || sample_rate % 100U || (!pcm && count))
            throw std::invalid_argument("Invalid Speex AGC PCM");
        if (state_ && sample_rate_ != sample_rate)
            throw std::invalid_argument("Speex AGC sample rate changed");
        if (!state_) {
            sample_rate_ = sample_rate;
            frame_size_ = sample_rate / 100U;
            state_ = speex_preprocess_state_init(static_cast<int>(frame_size_),
                                                static_cast<int>(sample_rate));
            if (!state_) throw std::runtime_error("Speex AGC initialization failed");
            spx_int32_t on = 1, off = 0, max_gain = 36;
            float level = 3500.0F;
            if (speex_preprocess_ctl(state_, SPEEX_PREPROCESS_SET_AGC, &on) ||
                speex_preprocess_ctl(state_, SPEEX_PREPROCESS_SET_DENOISE, &off) ||
                speex_preprocess_ctl(state_, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &max_gain) ||
                speex_preprocess_ctl(state_, SPEEX_PREPROCESS_SET_AGC_LEVEL, &level)) {
                reset();
                throw std::runtime_error("Speex AGC configuration failed");
            }
            input_.reserve(frame_size_);
        }
        std::vector<float> output(count, 0.0F);
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite Speex AGC sample");
            const double sample = std::clamp(static_cast<double>(pcm[i]), -1.0, 1.0);
            input_.push_back(static_cast<spx_int16_t>(std::lround(sample * 32767.0)));
            if (input_.size() == frame_size_) {
                speex_preprocess_run(state_, input_.data());
                for (const auto value : input_)
                    ready_.push_back(static_cast<float>(value) / 32768.0F);
                input_.clear();
                spx_int32_t percent = 100;
                if (!speex_preprocess_ctl(state_, SPEEX_PREPROCESS_GET_AGC_GAIN, &percent)
                    && percent > 0)
                    gain_db_ = 20.0 * std::log10(static_cast<double>(percent) / 100.0);
            }
            if (!ready_.empty()) {
                output[i] = ready_.front();
                ready_.pop_front();
            }
        }
        return output;
    }
    double gainDb() const noexcept override { return gain_db_; }
    void reset() noexcept override {
        if (state_) speex_preprocess_state_destroy(state_);
        state_ = nullptr;
        sample_rate_ = 0U;
        frame_size_ = 0U;
        gain_db_ = 0.0;
        input_.clear();
        ready_.clear();
    }
private:
    SpeexPreprocessState* state_{nullptr};
    std::uint32_t sample_rate_{0U};
    std::size_t frame_size_{0U};
    double gain_db_{0.0};
    std::vector<spx_int16_t> input_{};
    std::deque<float> ready_{};
};
#endif


#if defined(LIBAUDITION_WITH_WEBRTC_AGC2) && LIBAUDITION_WITH_WEBRTC_AGC2
// Standalone WebRTC AGC2 adapter. Mono FloatS16, 10-ms frames, with
// per-source state and bounded framing latency for arbitrary ODAS hops.
#if defined(LIBAUDITION_AGC2_PRIVATE_NAMESPACE) && LIBAUDITION_AGC2_PRIVATE_NAMESPACE
namespace agc2_webrtc = libaudition_agc2_webrtc;
#else
namespace agc2_webrtc = webrtc;
#endif
class WebRtcAgc2SourceGain final : public ISourceGain {
public:
    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if (!sample_rate || (!pcm && count) ||
            (sample_rate != 8000U && sample_rate != 16000U &&
             sample_rate != 32000U && sample_rate != 48000U))
            throw std::invalid_argument("WebRTC AGC2 requires 8/16/32/48 kHz mono PCM");
        if (engine_ && sample_rate != sample_rate_)
            throw std::invalid_argument("WebRTC AGC2 rate changed; reset first");
        if (!engine_) initialize(sample_rate);
        std::vector<float> out(count, 0.0F);
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite WebRTC AGC2 PCM");
            input_.push_back(static_cast<float>(
                std::clamp(static_cast<double>(pcm[i]), -1.0, 1.0) * 32767.0));
            if (input_.size() == frame_size_) {
                float* const channel = buffer_->channels()[0];
                std::copy(input_.begin(), input_.end(), channel);
                double in_power = 0.0;
                for (const float value : input_)
                    in_power += static_cast<double>(value) * value;
                engine_->Analyze(255, *buffer_);
                engine_->Process(false, buffer_.get());
                double out_power = 0.0;
                for (std::size_t j = 0; j < frame_size_; ++j)
                    out_power += static_cast<double>(channel[j]) * channel[j];
                if (in_power > 1e-8 && out_power > 1e-8)
                    gain_db_ = 10.0 * std::log10(out_power / in_power);
                for (std::size_t j = 0; j < frame_size_; ++j)
                    ready_.push_back(static_cast<float>(
                        std::clamp(static_cast<double>(channel[j]) / 32768.0,
                                   -1.0, 1.0)));
                input_.clear();
            }
            if (!ready_.empty()) {
                out[i] = ready_.front();
                ready_.pop_front();
            }
        }
        return out;
    }
    double gainDb() const noexcept override { return gain_db_; }
    void reset() noexcept override {
        engine_.reset();
        buffer_.reset();
        input_.clear();
        ready_.clear();
        sample_rate_ = 0U;
        frame_size_ = 0U;
        gain_db_ = 0.0;
    }
private:
    void initialize(std::uint32_t rate) {
        sample_rate_ = rate;
        frame_size_ = rate / 100U;
        agc2_webrtc::AudioProcessing::Config::GainController2 cfg{};
        cfg.enabled = true;
        cfg.adaptive_digital.enabled = true;
        cfg.adaptive_digital.max_gain_db = 36.0F;
        cfg.input_volume_controller.enabled = false;
        engine_ = std::make_unique<agc2_webrtc::GainController2>(
            environment_, cfg, agc2_webrtc::InputVolumeController::Config{},
            static_cast<int>(rate), 1, true);
        buffer_ = std::make_unique<agc2_webrtc::AudioBuffer>(rate, 1, rate, 1, rate, 1);
        input_.reserve(frame_size_);
    }
    agc2_webrtc::Environment environment_{};
    std::unique_ptr<agc2_webrtc::GainController2> engine_{};
    std::unique_ptr<agc2_webrtc::AudioBuffer> buffer_{};
    std::uint32_t sample_rate_{0};
    std::size_t frame_size_{0};
    double gain_db_{0.0};
    std::vector<float> input_{};
    std::deque<float> ready_{};
};
#endif


#if defined(LIBAUDITION_WITH_WEBRTC_APM) && LIBAUDITION_WITH_WEBRTC_APM
// One standalone APM session per separated source. AGC1 and AGC2 are mutually
// exclusive. AEC and NS remain off here: echo cancellation needs the original
// microphone channels and the render/reference stream, not separated speech.
class WebRtcApmSourceGain final : public ISourceGain {
public:
    explicit WebRtcApmSourceGain(bool use_agc2) : use_agc2_(use_agc2) {}

    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if ((!pcm && count != 0U) ||
            (sample_rate != 16000U && sample_rate != 32000U &&
             sample_rate != 48000U))
            throw std::invalid_argument("WebRTC APM needs 16/32/48 kHz mono PCM");
        if (engine_ && sample_rate != sample_rate_)
            throw std::invalid_argument("WebRTC APM sample rate changed; reset first");
        if (!engine_) initialize(sample_rate);

        std::vector<float> result(count, 0.0F);
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]))
                throw std::invalid_argument("Nonfinite WebRTC APM PCM");
            input_.push_back(pcm[i]);
            if (input_.size() == frame_size_) {
                std::vector<float> processed(frame_size_);
                const float* const source_channels[] = {input_.data()};
                float* const output_channels[] = {processed.data()};
                const webrtc::StreamConfig stream(
                    static_cast<int>(sample_rate_), 1);
                const int rc = engine_->ProcessStream(
                    source_channels, stream, stream, output_channels);
                if (rc != webrtc::AudioProcessing::kNoError)
                    throw std::runtime_error("WebRTC APM ProcessStream failed: " +
                                             std::to_string(rc));
                double input_power = 0.0;
                double output_power = 0.0;
                for (std::size_t j = 0; j < frame_size_; ++j) {
                    input_power += static_cast<double>(input_[j]) * input_[j];
                    output_power += static_cast<double>(processed[j]) * processed[j];
                    ready_.push_back(std::clamp(processed[j], -1.0F, 1.0F));
                }
                if (input_power > 1.e-12 && output_power > 1.e-12)
                    gain_db_ = 10.0 * std::log10(output_power / input_power);
                input_.clear();
            }
            if (!ready_.empty()) {
                result[i] = ready_.front();
                ready_.pop_front();
            }
        }
        return result;
    }

    double gainDb() const noexcept override { return gain_db_; }

    void reset() noexcept override {
        engine_ = nullptr;
        input_.clear();
        ready_.clear();
        sample_rate_ = 0;
        frame_size_ = 0;
        gain_db_ = 0.0;
    }

private:
    void initialize(std::uint32_t rate) {
        auto engine = webrtc::AudioProcessingBuilder().Create();
        if (!engine)
            throw std::runtime_error("WebRTC APM initialization failed");
        webrtc::AudioProcessing::Config cfg{};
        cfg.echo_canceller.enabled = false;
        cfg.noise_suppression.enabled = false;
        cfg.high_pass_filter.enabled = false;
        cfg.gain_controller1.enabled = !use_agc2_;
        cfg.gain_controller1.mode =
            webrtc::AudioProcessing::Config::GainController1::Mode::kAdaptiveDigital;
        cfg.gain_controller1.analog_gain_controller.enabled = false;
        cfg.gain_controller2.enabled = use_agc2_;
        cfg.gain_controller2.adaptive_digital.enabled = use_agc2_;
        engine->ApplyConfig(cfg);
        const webrtc::ProcessingConfig stream_config{
            webrtc::StreamConfig(static_cast<int>(rate), 1),
            webrtc::StreamConfig(static_cast<int>(rate), 1),
            webrtc::StreamConfig(static_cast<int>(rate), 1),
            webrtc::StreamConfig(static_cast<int>(rate), 1)};
        const int rc = engine->Initialize(stream_config);
        if (rc != webrtc::AudioProcessing::kNoError)
            throw std::runtime_error("WebRTC APM Initialize failed: " +
                                     std::to_string(rc));
        engine_ = std::move(engine);
        sample_rate_ = rate;
        frame_size_ = rate / 100U;
        input_.reserve(frame_size_);
    }

    bool use_agc2_{true};
    webrtc::scoped_refptr<webrtc::AudioProcessing> engine_{};
    std::uint32_t sample_rate_{0U};
    std::size_t frame_size_{0U};
    double gain_db_{0.0};
    std::vector<float> input_{};
    std::deque<float> ready_{};
};
#endif

// Factory is intentionally replaceable by applications and later algorithms.
inline std::unique_ptr<ISourceGain> makeSourceGain(const char* implementation) {
    if (implementation && std::string(implementation) == "rms")
        return std::make_unique<RmsSourceGain>();
#if defined(LIBAUDITION_WITH_SPEEXDSP_AGC) && LIBAUDITION_WITH_SPEEXDSP_AGC
    if (implementation && std::string(implementation) == "speex")
        return std::make_unique<SpeexSourceGain>();
#endif
#if defined(LIBAUDITION_WITH_WEBRTC_AGC2) && LIBAUDITION_WITH_WEBRTC_AGC2
    if (implementation && std::string(implementation) == "webrtc_agc2")
        return std::make_unique<WebRtcAgc2SourceGain>();
#endif
#if defined(LIBAUDITION_WITH_WEBRTC_APM) && LIBAUDITION_WITH_WEBRTC_APM
    if (implementation && std::string(implementation) == "webrtc_apm_agc1")
        return std::make_unique<WebRtcApmSourceGain>(false);
    if (implementation && std::string(implementation) == "webrtc_apm_agc2")
        return std::make_unique<WebRtcApmSourceGain>(true);
#endif
    throw std::invalid_argument("Unknown source gain implementation");
}
} // namespace audition::dsp
