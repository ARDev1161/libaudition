#include <audition/backends/world/voice_traits_estimator.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

#include <world/dio.h>
#include <world/harvest.h>
#include <world/stonemask.h>

namespace audition {
namespace {

void requireConfiguration(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

bool cpuProviderSelected(const ExecutionTarget& execution) {
    return execution.provider.empty() ||
           execution.provider == "cpu";
}

void validateExecution(const ExecutionTarget& execution) {
    requireConfiguration(
        execution.device_class == DeviceClass::Auto ||
            execution.device_class == DeviceClass::Cpu,
        "WORLD backend supports CPU execution only");
    requireConfiguration(
        cpuProviderSelected(execution),
        "WORLD backend supports only the CPU provider");
    requireConfiguration(
        execution.device_index < 0,
        "WORLD CPU backend does not support device_index");
    requireConfiguration(
        execution.precision == PrecisionPreference::Auto ||
            execution.precision == PrecisionPreference::Float32,
        "WORLD backend accepts float32 libaudition PCM input only");
    requireConfiguration(
        execution.provider_options.empty(),
        "WORLD backend does not expose provider_options");
}

void validateAudio(
    AudioView audio,
    const WorldVoiceTraitsOptions& options) {
    if (!audio.format().valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "WORLD voice analysis requires a valid audio format"};
    }
    if (audio.format().channel_count != 1U) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "WORLD voice analysis requires mono audio"};
    }
    if (audio.sampleCount() == 0U) {
        throw Error{
            ErrorCode::InvalidArgument,
            "WORLD voice analysis requires non-empty audio"};
    }
    if (audio.sampleCount() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        throw Error{
            ErrorCode::InvalidArgument,
            "WORLD input exceeds backend sample-count range"};
    }
    if (audio.format().sample_rate_hz >
        static_cast<std::uint32_t>(
            std::numeric_limits<int>::max())) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "WORLD input sample rate exceeds backend range"};
    }

    const double nyquist_hz =
        static_cast<double>(
            audio.format().sample_rate_hz) / 2.0;
    if (options.f0_ceil_hz >= nyquist_hz) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "WORLD f0_ceil_hz must be below the input Nyquist frequency"};
    }
}

std::vector<double> toWorldSamples(AudioView audio) {
    std::vector<double> samples(audio.sampleCount());
    for (std::size_t i = 0U;
         i < audio.sampleCount();
         ++i) {
        const double value =
            static_cast<double>(audio.data()[i]);
        if (!std::isfinite(value)) {
            throw Error{
                ErrorCode::InvalidArgument,
                "WORLD input contains a non-finite sample"};
        }
        samples[i] = value;
    }
    return samples;
}

std::vector<double> estimateDio(
    const std::vector<double>& samples,
    int sample_rate_hz,
    const WorldVoiceTraitsOptions& options) {
    DioOption native_options{};
    InitializeDioOption(&native_options);
    native_options.frame_period =
        options.frame_period_ms;
    native_options.f0_floor =
        options.f0_floor_hz;
    native_options.f0_ceil =
        options.f0_ceil_hz;
    native_options.speed =
        options.dio_speed;
    native_options.allowed_range =
        options.dio_allowed_range;

    const int length =
        GetSamplesForDIO(
            sample_rate_hz,
            static_cast<int>(samples.size()),
            native_options.frame_period);
    if (length <= 0) {
        throw Error{
            ErrorCode::ProcessingError,
            "WORLD DIO produced an invalid contour length"};
    }

    std::vector<double> time_axis(
        static_cast<std::size_t>(length));
    std::vector<double> initial_f0(
        static_cast<std::size_t>(length));
    std::vector<double> refined_f0(
        static_cast<std::size_t>(length));

    Dio(samples.data(),
        static_cast<int>(samples.size()),
        sample_rate_hz,
        &native_options,
        time_axis.data(),
        initial_f0.data());

    StoneMask(samples.data(),
              static_cast<int>(samples.size()),
              sample_rate_hz,
              time_axis.data(),
              initial_f0.data(),
              length,
              refined_f0.data());

    return refined_f0;
}

std::vector<double> estimateHarvest(
    const std::vector<double>& samples,
    int sample_rate_hz,
    const WorldVoiceTraitsOptions& options) {
    HarvestOption native_options{};
    InitializeHarvestOption(&native_options);
    native_options.frame_period =
        options.frame_period_ms;
    native_options.f0_floor =
        options.f0_floor_hz;
    native_options.f0_ceil =
        options.f0_ceil_hz;

    const int length =
        GetSamplesForHarvest(
            sample_rate_hz,
            static_cast<int>(samples.size()),
            native_options.frame_period);
    if (length <= 0) {
        throw Error{
            ErrorCode::ProcessingError,
            "WORLD Harvest produced an invalid contour length"};
    }

    std::vector<double> time_axis(
        static_cast<std::size_t>(length));
    std::vector<double> f0(
        static_cast<std::size_t>(length));

    Harvest(samples.data(),
            static_cast<int>(samples.size()),
            sample_rate_hz,
            &native_options,
            time_axis.data(),
            f0.data());

    return f0;
}

VoiceTraits summarizeF0(
    const std::vector<double>& contour,
    const WorldVoiceTraitsOptions& options) {
    std::vector<double> voiced;
    voiced.reserve(contour.size());

    for (const double f0 : contour) {
        if (std::isfinite(f0) &&
            f0 >= options.f0_floor_hz &&
            f0 <= options.f0_ceil_hz) {
            voiced.push_back(f0);
        }
    }

    VoiceTraits traits{};
    if (voiced.size() <
        options.minimum_voiced_frames) {
        return traits;
    }

    double sum = 0.0;
    for (const double f0 : voiced) {
        sum += f0;
    }
    const double mean =
        sum / static_cast<double>(voiced.size());

    double squared_deviation_sum = 0.0;
    for (const double f0 : voiced) {
        const double deviation = f0 - mean;
        squared_deviation_sum +=
            deviation * deviation;
    }
    const double variance =
        squared_deviation_sum /
        static_cast<double>(voiced.size());
    const double standard_deviation =
        std::sqrt(variance);

    if (!std::isfinite(mean) ||
        !std::isfinite(standard_deviation)) {
        throw Error{
            ErrorCode::ProcessingError,
            "WORLD produced invalid pitch statistics"};
    }

    traits.pitch_mean_hz = mean;
    traits.pitch_stddev_hz =
        standard_deviation;
    return traits;
}

}  // namespace

void validateWorldVoiceTraitsOptions(
    const WorldVoiceTraitsOptions& options) {
    validateExecution(options.execution);

    requireConfiguration(
        std::isfinite(options.frame_period_ms) &&
            options.frame_period_ms > 0.0 &&
            options.frame_period_ms <= 100.0,
        "WORLD frame_period_ms must be finite and in (0, 100]");
    requireConfiguration(
        std::isfinite(options.f0_floor_hz) &&
            options.f0_floor_hz > 0.0,
        "WORLD f0_floor_hz must be finite and positive");
    requireConfiguration(
        std::isfinite(options.f0_ceil_hz) &&
            options.f0_ceil_hz >
                options.f0_floor_hz,
        "WORLD f0_ceil_hz must be finite and greater than f0_floor_hz");
    requireConfiguration(
        options.dio_speed >= 1 &&
            options.dio_speed <= 12,
        "WORLD DIO speed must be in [1, 12]");
    requireConfiguration(
        std::isfinite(
            options.dio_allowed_range) &&
            options.dio_allowed_range >= 0.0,
        "WORLD DIO allowed range must be finite and non-negative");
    requireConfiguration(
        options.minimum_voiced_frames > 0U,
        "WORLD minimum_voiced_frames must be positive");
}

class WorldVoiceTraitsEstimator::Impl {
public:
    explicit Impl(WorldVoiceTraitsOptions options)
        : options_(std::move(options)) {
        validateWorldVoiceTraitsOptions(options_);
    }

    [[nodiscard]] VoiceTraits estimate(
        AudioView speech) const {
        validateAudio(speech, options_);
        const auto samples =
            toWorldSamples(speech);

        const int sample_rate_hz =
            static_cast<int>(
                speech.format().sample_rate_hz);

        std::vector<double> f0;
        switch (options_.algorithm) {
        case WorldF0Algorithm::DioStoneMask:
            f0 = estimateDio(
                samples,
                sample_rate_hz,
                options_);
            break;
        case WorldF0Algorithm::Harvest:
            f0 = estimateHarvest(
                samples,
                sample_rate_hz,
                options_);
            break;
        }

        return summarizeF0(f0, options_);
    }

    WorldVoiceTraitsOptions options_{};
};

WorldVoiceTraitsEstimator::
    WorldVoiceTraitsEstimator(
        WorldVoiceTraitsOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(options))) {}

WorldVoiceTraitsEstimator::
    ~WorldVoiceTraitsEstimator() = default;

WorldVoiceTraitsEstimator::
    WorldVoiceTraitsEstimator(
        WorldVoiceTraitsEstimator&&) noexcept =
    default;

WorldVoiceTraitsEstimator&
WorldVoiceTraitsEstimator::operator=(
    WorldVoiceTraitsEstimator&&) noexcept =
    default;

BackendInfo
WorldVoiceTraitsEstimator::backendInfo() const {
    return {"world", ""};
}

VoiceTraitsCapabilities
WorldVoiceTraitsEstimator::capabilities() const {
    VoiceTraitsCapabilities capabilities{};
    capabilities.pitch_statistics = true;
    capabilities.estimated_age = false;
    capabilities.categorical_traits = false;
    capabilities.speaking_rate = false;
    capabilities.audio.min_channels = 1U;
    capabilities.audio.max_channels = 1U;
    capabilities.audio.supported_layouts = {
        AudioLayout::Interleaved,
        AudioLayout::Planar};
    capabilities.execution.device_classes = {
        DeviceClass::Cpu};
    capabilities.execution.providers = {"cpu"};
    return capabilities;
}

VoiceTraits
WorldVoiceTraitsEstimator::estimate(
    AudioView speech) const {
    return impl_->estimate(speech);
}

const WorldVoiceTraitsOptions&
WorldVoiceTraitsEstimator::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
