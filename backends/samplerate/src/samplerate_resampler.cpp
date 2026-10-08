#include <audition/backends/samplerate/samplerate_resampler.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <samplerate.h>

#include <audition/core/error.hpp>
#include <audition/dsp/basic.hpp>

namespace audition {
namespace {

int converterType(SamplerateConverter converter) {
    switch (converter) {
        case SamplerateConverter::SincBest:
            return SRC_SINC_BEST_QUALITY;
        case SamplerateConverter::SincMedium:
            return SRC_SINC_MEDIUM_QUALITY;
        case SamplerateConverter::SincFast:
            return SRC_SINC_FASTEST;
        case SamplerateConverter::ZeroOrderHold:
            return SRC_ZERO_ORDER_HOLD;
        case SamplerateConverter::Linear:
            return SRC_LINEAR;
    }
    throw Error{ErrorCode::ConfigurationError, "Unknown libsamplerate converter"};
}

void validateConfig(const ResamplerConfig& config) {
    if (config.input_sample_rate_hz == 0U || config.output_sample_rate_hz == 0U ||
        config.channel_count == 0U) {
        throw Error{ErrorCode::ConfigurationError,
                    "ResamplerConfig rates and channel count must be non-zero"};
    }
}

std::string samplerateError(int code) {
    const char* message = src_strerror(code);
    return message != nullptr ? std::string{message} : std::string{"unknown libsamplerate error"};
}

class SamplerateSession final : public IAudioResamplerSession {
public:
    SamplerateSession(ResamplerConfig config, SamplerateOptions options)
        : config_(config), options_(options) {
        validateConfig(config_);
        int error = 0;
        state_ = src_new(converterType(options_.converter),
                         static_cast<int>(config_.channel_count), &error);
        if (state_ == nullptr) {
            throw Error{ErrorCode::BackendUnavailable,
                        "libsamplerate src_new failed: " + samplerateError(error)};
        }
    }

    ~SamplerateSession() override {
        state_ = src_delete(state_);
    }

    void reset() override {
        const int status = src_reset(state_);
        if (status != 0) {
            throw Error{ErrorCode::ProcessingError,
                        "libsamplerate reset failed: " + samplerateError(status)};
        }
        flushed_ = false;
        last_layout_.reset();
        next_output_time_.reset();
        last_sequence_number_ = 0U;
    }

    AudioBuffer process(AudioView audio) override {
        if (flushed_) {
            throw Error{ErrorCode::InvalidState,
                        "Resampler session must be reset before processing after flush"};
        }
        validateInput(audio);
        last_layout_ = audio.format().layout;
        last_sequence_number_ = audio.sequenceNumber();

        AudioBuffer interleaved_storage{};
        AudioView interleaved = audio;
        if (audio.format().layout != AudioLayout::Interleaved) {
            interleaved_storage = dsp::convertLayout(audio, AudioLayout::Interleaved);
            interleaved = interleaved_storage.view();
        }

        const double ratio =
            static_cast<double>(config_.output_sample_rate_hz) /
            static_cast<double>(config_.input_sample_rate_hz);
        const auto input_frames = interleaved.frameCount();

        std::vector<float> output;
        output.reserve(static_cast<std::size_t>(
                           std::ceil(static_cast<double>(input_frames) * ratio)) *
                       config_.channel_count +
                       256U * config_.channel_count);

        std::size_t consumed_frames = 0U;
        while (consumed_frames < input_frames) {
            const auto remaining = input_frames - consumed_frames;
            const auto output_capacity_frames =
                std::max<std::size_t>(
                    static_cast<std::size_t>(
                        std::ceil(static_cast<double>(remaining) * ratio)) + 256U,
                    256U);
            const auto old_size = output.size();
            output.resize(old_size +
                          output_capacity_frames * config_.channel_count);

            SRC_DATA data{};
            data.data_in =
                interleaved.data() +
                consumed_frames * static_cast<std::size_t>(config_.channel_count);
            data.input_frames = static_cast<long>(remaining);
            data.data_out = output.data() + old_size;
            data.output_frames = static_cast<long>(output_capacity_frames);
            data.end_of_input = 0;
            data.src_ratio = ratio;

            const int status = src_process(state_, &data);
            if (status != 0) {
                throw Error{ErrorCode::ProcessingError,
                            "libsamplerate processing failed: " + samplerateError(status)};
            }

            consumed_frames += static_cast<std::size_t>(data.input_frames_used);
            output.resize(old_size +
                          static_cast<std::size_t>(data.output_frames_gen) *
                              config_.channel_count);

            if (data.input_frames_used == 0 && data.output_frames_gen == 0) {
                throw Error{ErrorCode::ProcessingError,
                            "libsamplerate made no progress while input remained"};
            }
        }

        const Timestamp timestamp =
            next_output_time_.has_value() ? *next_output_time_ : audio.captureTime();
        AudioBuffer result{
            std::move(output),
            AudioFormat{config_.output_sample_rate_hz, config_.channel_count,
                        AudioLayout::Interleaved},
            timestamp,
            audio.sequenceNumber()};

        next_output_time_ = timestamp.advancedBy(result.duration());

        if (*last_layout_ == AudioLayout::Planar) {
            return dsp::convertLayout(result.view(), AudioLayout::Planar);
        }
        return result;
    }

    AudioBuffer flush() override {
        if (flushed_) {
            return emptyOutput();
        }
        flushed_ = true;

        const double ratio =
            static_cast<double>(config_.output_sample_rate_hz) /
            static_cast<double>(config_.input_sample_rate_hz);
        std::vector<float> output;

        while (true) {
            constexpr std::size_t kFlushFrames = 512U;
            const auto old_size = output.size();
            output.resize(old_size + kFlushFrames * config_.channel_count);

            SRC_DATA data{};
            data.data_in = nullptr;
            data.input_frames = 0;
            data.data_out = output.data() + old_size;
            data.output_frames = static_cast<long>(kFlushFrames);
            data.end_of_input = 1;
            data.src_ratio = ratio;

            const int status = src_process(state_, &data);
            if (status != 0) {
                throw Error{ErrorCode::ProcessingError,
                            "libsamplerate flush failed: " + samplerateError(status)};
            }

            output.resize(old_size +
                          static_cast<std::size_t>(data.output_frames_gen) *
                              config_.channel_count);
            if (data.output_frames_gen == 0) {
                break;
            }
        }

        const Timestamp timestamp = next_output_time_.value_or(Timestamp{});
        AudioBuffer result{
            std::move(output),
            AudioFormat{config_.output_sample_rate_hz, config_.channel_count,
                        AudioLayout::Interleaved},
            timestamp,
            last_sequence_number_};

        next_output_time_ = timestamp.advancedBy(result.duration());

        if (last_layout_.value_or(AudioLayout::Interleaved) == AudioLayout::Planar) {
            return dsp::convertLayout(result.view(), AudioLayout::Planar);
        }
        return result;
    }

private:
    void validateInput(AudioView audio) const {
        if (!audio.format().valid()) {
            throw Error{ErrorCode::InvalidArgument,
                        "Resampler requires a valid input audio format"};
        }
        if (audio.format().sample_rate_hz != config_.input_sample_rate_hz ||
            audio.format().channel_count != config_.channel_count) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "Resampler input format does not match session configuration"};
        }
    }

    AudioBuffer emptyOutput() const {
        return AudioBuffer{
            {},
            AudioFormat{config_.output_sample_rate_hz, config_.channel_count,
                        last_layout_.value_or(AudioLayout::Interleaved)},
            next_output_time_.value_or(Timestamp{}),
            last_sequence_number_};
    }

    ResamplerConfig config_{};
    SamplerateOptions options_{};
    SRC_STATE* state_{nullptr};
    bool flushed_{false};
    std::optional<AudioLayout> last_layout_{};
    std::optional<Timestamp> next_output_time_{};
    std::uint64_t last_sequence_number_{0U};
};

}  // namespace

class SamplerateResampler::Impl {
public:
    explicit Impl(SamplerateOptions options) : options_(options) {}

    SamplerateOptions options_{};
};

SamplerateResampler::SamplerateResampler(SamplerateOptions options)
    : impl_(std::make_unique<Impl>(options)) {}
SamplerateResampler::~SamplerateResampler() = default;
SamplerateResampler::SamplerateResampler(SamplerateResampler&&) noexcept = default;
SamplerateResampler& SamplerateResampler::operator=(SamplerateResampler&&) noexcept = default;

BackendInfo SamplerateResampler::backendInfo() const {
    return {"libsamplerate", src_get_version()};
}

std::unique_ptr<IAudioResamplerSession> SamplerateResampler::createSession(
    const ResamplerConfig& config) const {
    validateConfig(config);
    return std::make_unique<SamplerateSession>(config, impl_->options_);
}

const SamplerateOptions& SamplerateResampler::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
