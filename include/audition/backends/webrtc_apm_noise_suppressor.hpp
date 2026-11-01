#pragma once

#include <audition/dsp/source_noise_suppressor.hpp>
#include <modules/audio_processing/include/audio_processing.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace audition::dsp {

// Standalone APM NS on mono separated speech PCM, NOT raw ODAS input.
// AEC, AGC1, AGC2 and HPF are explicitly disabled. One instance per source;
// not thread-safe. Calls allocate output vectors; never use on a strict
// realtime capture callback. Returns count samples in capture order.
// Initially emits (sample_rate / 100 - 1) zero samples. reset() discards
// partial frames on retirement, sample-rate change and track-ID reuse.
class WebRtcApmSourceNoiseSuppressor final : public ISourceNoiseSuppressor {
public:
    explicit WebRtcApmSourceNoiseSuppressor(
        NoiseSuppressionLevel level = NoiseSuppressionLevel::moderate)
        : level_(level) {
        (void)toWebRtcLevel(level_);
    }

    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t sample_rate) override {
        if ((sample_rate != 16000U && sample_rate != 32000U &&
             sample_rate != 48000U) || (!pcm && count != 0U))
            throw std::invalid_argument(
                "WebRTC APM NS requires normalized mono PCM at 16/32/48 kHz");
        if (engine_ && sample_rate != sample_rate_)
            throw std::invalid_argument("WebRTC APM NS rate changed; reset first");
        // A rejected chunk must never leave a valid prefix in the APM input.
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(pcm[i]) || std::abs(pcm[i]) > 1.0F)
                throw std::invalid_argument("Invalid WebRTC APM NS sample");
        }
        if (count == 0U) return {};
        if (!engine_) initialize(sample_rate);
        std::vector<float> result(count, 0.0F);
        const webrtc::StreamConfig mono(static_cast<int>(sample_rate_), 1);
        for (std::size_t i = 0; i < count; ++i) {
            input_.push_back(pcm[i]);
            if (input_.size() == frame_size_) {
                const float* const source_channels[] = {input_.data()};
                float* const output_channels[] = {processed_.data()};
                const int rc = engine_->ProcessStream(
                    source_channels, mono, mono, output_channels);
                if (rc != webrtc::AudioProcessing::kNoError) {
                    reset();
                    throw std::runtime_error("WebRTC APM NS ProcessStream failed: " +
                                             std::to_string(rc));
                }
                input_.clear();
                next_output_index_ = 0U;
            }
            if (next_output_index_ < processed_.size()) {
                result[i] = std::clamp(
                    processed_[next_output_index_++], -1.0F, 1.0F);
            }
        }
        return result;
    }

    void reset() noexcept override {
        engine_ = nullptr;
        input_.clear();
        processed_.clear();
        frame_size_ = 0U;
        sample_rate_ = 0U;
        next_output_index_ = 0U;
    }

private:
    using NsLevel = webrtc::AudioProcessing::Config::NoiseSuppression::Level;

    static NsLevel toWebRtcLevel(NoiseSuppressionLevel level) {
        switch (level) {
        case NoiseSuppressionLevel::low: return NsLevel::kLow;
        case NoiseSuppressionLevel::moderate: return NsLevel::kModerate;
        case NoiseSuppressionLevel::high: return NsLevel::kHigh;
        case NoiseSuppressionLevel::very_high: return NsLevel::kVeryHigh;
        }
        throw std::invalid_argument("Invalid WebRTC APM NS level");
    }

    void initialize(std::uint32_t rate) {
        auto engine = webrtc::AudioProcessingBuilder().Create();
        if (!engine)
            throw std::runtime_error("WebRTC APM NS initialization failed");
        webrtc::AudioProcessing::Config cfg{};
        cfg.echo_canceller.enabled = false;
        cfg.gain_controller1.enabled = false;
        cfg.gain_controller2.enabled = false;
        cfg.high_pass_filter.enabled = false;
        cfg.noise_suppression.enabled = true;
        cfg.noise_suppression.level = toWebRtcLevel(level_);
        engine->ApplyConfig(cfg);
        const webrtc::ProcessingConfig streams{
            webrtc::StreamConfig(static_cast<int>(rate), 1),
            webrtc::StreamConfig(static_cast<int>(rate), 1),
            webrtc::StreamConfig(static_cast<int>(rate), 1),
            webrtc::StreamConfig(static_cast<int>(rate), 1)};
        const int rc = engine->Initialize(streams);
        if (rc != webrtc::AudioProcessing::kNoError)
            throw std::runtime_error("WebRTC APM NS Initialize failed: " +
                                     std::to_string(rc));
        const std::size_t frame_size = rate / 100U;
        input_.reserve(frame_size);
        processed_.resize(frame_size);
        frame_size_ = frame_size;
        next_output_index_ = frame_size;
        sample_rate_ = rate;
        engine_ = std::move(engine);
    }

    NoiseSuppressionLevel level_;
    webrtc::scoped_refptr<webrtc::AudioProcessing> engine_{};
    std::uint32_t sample_rate_{0U};
    std::size_t frame_size_{0U};
    std::size_t next_output_index_{0U};
    std::vector<float> input_{};
    std::vector<float> processed_{};
};

inline std::unique_ptr<ISourceNoiseSuppressor>
makeWebRtcApmSourceNoiseSuppressor(
    NoiseSuppressionLevel level = NoiseSuppressionLevel::moderate) {
    return std::make_unique<WebRtcApmSourceNoiseSuppressor>(level);
}

} // namespace audition::dsp
