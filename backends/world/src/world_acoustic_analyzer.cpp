#include <audition/backends/world/acoustic_analyzer.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

#include <world/cheaptrick.h>
#include <world/d4c.h>
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
    const WorldAcousticAnalysisOptions& options) {
    if (!audio.format().valid()) {
        throw Error{
            ErrorCode::InvalidArgument,
            "WORLD acoustic analysis requires a valid audio format"};
    }
    if (audio.format().channel_count != 1U) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "WORLD acoustic analysis requires mono audio"};
    }
    if (audio.sampleCount() == 0U) {
        throw Error{
            ErrorCode::InvalidArgument,
            "WORLD acoustic analysis requires non-empty audio"};
    }
    if (audio.sampleCount() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw Error{
            ErrorCode::InvalidArgument,
            "WORLD input exceeds backend sample-count range"};
    }
    if (audio.format().sample_rate_hz >
        static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "WORLD input sample rate exceeds backend range"};
    }

    const double nyquist_hz =
        static_cast<double>(audio.format().sample_rate_hz) / 2.0;
    if (options.f0_ceil_hz >= nyquist_hz) {
        throw Error{
            ErrorCode::UnsupportedFormat,
            "WORLD f0_ceil_hz must be below the input Nyquist frequency"};
    }
}

std::vector<double> toWorldSamples(AudioView audio) {
    std::vector<double> samples(audio.sampleCount());
    for (std::size_t i = 0U; i < audio.sampleCount(); ++i) {
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

struct F0Analysis {
    std::vector<double> time_axis{};
    std::vector<double> f0{};
};

F0Analysis estimateDio(
    const std::vector<double>& samples,
    int sample_rate_hz,
    const WorldAcousticAnalysisOptions& options) {
    DioOption native_options{};
    InitializeDioOption(&native_options);
    native_options.frame_period = options.frame_period_ms;
    native_options.f0_floor = options.f0_floor_hz;
    native_options.f0_ceil = options.f0_ceil_hz;
    native_options.speed = options.dio_speed;
    native_options.allowed_range = options.dio_allowed_range;

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

    F0Analysis result;
    result.time_axis.resize(static_cast<std::size_t>(length));
    result.f0.resize(static_cast<std::size_t>(length));
    std::vector<double> initial_f0(
        static_cast<std::size_t>(length));

    Dio(samples.data(),
        static_cast<int>(samples.size()),
        sample_rate_hz,
        &native_options,
        result.time_axis.data(),
        initial_f0.data());

    StoneMask(samples.data(),
              static_cast<int>(samples.size()),
              sample_rate_hz,
              result.time_axis.data(),
              initial_f0.data(),
              length,
              result.f0.data());

    return result;
}

F0Analysis estimateHarvest(
    const std::vector<double>& samples,
    int sample_rate_hz,
    const WorldAcousticAnalysisOptions& options) {
    HarvestOption native_options{};
    InitializeHarvestOption(&native_options);
    native_options.frame_period = options.frame_period_ms;
    native_options.f0_floor = options.f0_floor_hz;
    native_options.f0_ceil = options.f0_ceil_hz;

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

    F0Analysis result;
    result.time_axis.resize(static_cast<std::size_t>(length));
    result.f0.resize(static_cast<std::size_t>(length));

    Harvest(samples.data(),
            static_cast<int>(samples.size()),
            sample_rate_hz,
            &native_options,
            result.time_axis.data(),
            result.f0.data());

    return result;
}

F0Analysis estimateF0(
    const std::vector<double>& samples,
    int sample_rate_hz,
    const WorldAcousticAnalysisOptions& options) {
    switch (options.algorithm) {
    case WorldF0Algorithm::DioStoneMask:
        return estimateDio(samples, sample_rate_hz, options);
    case WorldF0Algorithm::Harvest:
        return estimateHarvest(samples, sample_rate_hz, options);
    }
    throw Error{
        ErrorCode::ConfigurationError,
        "WORLD F0 algorithm is invalid"};
}

std::vector<double*> rowPointers(
    std::vector<std::vector<double>>& rows) {
    std::vector<double*> pointers;
    pointers.reserve(rows.size());
    for (auto& row : rows) {
        pointers.push_back(row.data());
    }
    return pointers;
}

std::vector<double> flattenAndValidate(
    const std::vector<std::vector<double>>& matrix,
    const char* name,
    bool require_non_negative) {
    std::size_t count = 0U;
    for (const auto& row : matrix) {
        count += row.size();
    }

    std::vector<double> flattened;
    flattened.reserve(count);

    for (const auto& row : matrix) {
        for (double value : row) {
            if (!std::isfinite(value) ||
                (require_non_negative && value < 0.0)) {
                throw Error{
                    ErrorCode::ProcessingError,
                    std::string{"WORLD produced invalid "} + name};
            }
            flattened.push_back(value);
        }
    }
    return flattened;
}

}  // namespace

void validateWorldAcousticAnalysisOptions(
    const WorldAcousticAnalysisOptions& options) {
    validateExecution(options.execution);

    switch (options.algorithm) {
    case WorldF0Algorithm::DioStoneMask:
    case WorldF0Algorithm::Harvest:
        break;
    default:
        throw Error{
            ErrorCode::ConfigurationError,
            "WORLD F0 algorithm is invalid"};
    }

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
            options.f0_ceil_hz > options.f0_floor_hz,
        "WORLD f0_ceil_hz must be finite and greater than f0_floor_hz");
    requireConfiguration(
        options.dio_speed >= 1 &&
            options.dio_speed <= 12,
        "WORLD DIO speed must be in [1, 12]");
    requireConfiguration(
        std::isfinite(options.dio_allowed_range) &&
            options.dio_allowed_range >= 0.0,
        "WORLD DIO allowed range must be finite and non-negative");
    requireConfiguration(
        std::isfinite(options.cheaptrick_q1),
        "WORLD CheapTrick q1 must be finite");
    requireConfiguration(
        std::isfinite(options.d4c_threshold) &&
            options.d4c_threshold >= 0.0 &&
            options.d4c_threshold <= 1.0,
        "WORLD D4C threshold must be finite and in [0, 1]");
}

class WorldAcousticAnalyzer::Impl {
public:
    explicit Impl(WorldAcousticAnalysisOptions options)
        : options_(std::move(options)) {
        validateWorldAcousticAnalysisOptions(options_);
    }

    [[nodiscard]] VoiceAcousticFeatures analyze(
        AudioView speech) const {
        validateAudio(speech, options_);
        const auto samples = toWorldSamples(speech);
        const int sample_rate_hz =
            static_cast<int>(speech.format().sample_rate_hz);

        auto pitch =
            estimateF0(samples, sample_rate_hz, options_);
        if (pitch.time_axis.size() != pitch.f0.size() ||
            pitch.f0.empty()) {
            throw Error{
                ErrorCode::ProcessingError,
                "WORLD produced inconsistent F0 analysis dimensions"};
        }

        for (double value : pitch.time_axis) {
            if (!std::isfinite(value) || value < 0.0) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "WORLD produced an invalid time axis"};
            }
        }
        for (double value : pitch.f0) {
            if (!std::isfinite(value) || value < 0.0) {
                throw Error{
                    ErrorCode::ProcessingError,
                    "WORLD produced an invalid F0 contour"};
            }
        }

        CheapTrickOption cheaptrick{};
        InitializeCheapTrickOption(
            sample_rate_hz, &cheaptrick);
        cheaptrick.q1 = options_.cheaptrick_q1;
        cheaptrick.f0_floor = options_.f0_floor_hz;
        cheaptrick.fft_size =
            GetFFTSizeForCheapTrick(
                sample_rate_hz, &cheaptrick);

        if (cheaptrick.fft_size <= 0) {
            throw Error{
                ErrorCode::ProcessingError,
                "WORLD CheapTrick produced an invalid FFT size"};
        }

        const std::size_t frame_count = pitch.f0.size();
        const std::size_t bin_count =
            static_cast<std::size_t>(
                cheaptrick.fft_size / 2 + 1);

        std::vector<std::vector<double>> spectrum(
            frame_count,
            std::vector<double>(bin_count));
        auto spectrum_rows = rowPointers(spectrum);

        CheapTrick(
            samples.data(),
            static_cast<int>(samples.size()),
            sample_rate_hz,
            pitch.time_axis.data(),
            pitch.f0.data(),
            static_cast<int>(frame_count),
            &cheaptrick,
            spectrum_rows.data());

        D4COption d4c{};
        InitializeD4COption(&d4c);
        d4c.threshold = options_.d4c_threshold;

        std::vector<std::vector<double>> aperiodicity(
            frame_count,
            std::vector<double>(bin_count));
        auto aperiodicity_rows =
            rowPointers(aperiodicity);

        D4C(
            samples.data(),
            static_cast<int>(samples.size()),
            sample_rate_hz,
            pitch.time_axis.data(),
            pitch.f0.data(),
            static_cast<int>(frame_count),
            cheaptrick.fft_size,
            &d4c,
            aperiodicity_rows.data());

        VoiceAcousticFeatures result{};
        result.sample_rate_hz =
            speech.format().sample_rate_hz;
        result.frame_period_ms =
            options_.frame_period_ms;
        result.fft_size =
            static_cast<std::size_t>(
                cheaptrick.fft_size);
        result.frame_count = frame_count;
        result.frequency_bin_count = bin_count;
        result.time_axis_seconds =
            std::move(pitch.time_axis);
        result.f0_hz = std::move(pitch.f0);
        result.spectral_envelope =
            flattenAndValidate(
                spectrum,
                "spectral envelope",
                true);
        result.aperiodicity =
            flattenAndValidate(
                aperiodicity,
                "aperiodicity",
                true);

        return result;
    }

    WorldAcousticAnalysisOptions options_{};
};

WorldAcousticAnalyzer::WorldAcousticAnalyzer(
    WorldAcousticAnalysisOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(options))) {}

WorldAcousticAnalyzer::~WorldAcousticAnalyzer() = default;

WorldAcousticAnalyzer::WorldAcousticAnalyzer(
    WorldAcousticAnalyzer&&) noexcept = default;

WorldAcousticAnalyzer&
WorldAcousticAnalyzer::operator=(
    WorldAcousticAnalyzer&&) noexcept = default;

BackendInfo
WorldAcousticAnalyzer::backendInfo() const {
    return {"world", ""};
}

VoiceAcousticCapabilities
WorldAcousticAnalyzer::capabilities() const {
    VoiceAcousticCapabilities capabilities{};
    capabilities.f0_contour = true;
    capabilities.spectral_envelope = true;
    capabilities.aperiodicity = true;
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

VoiceAcousticFeatures
WorldAcousticAnalyzer::analyze(
    AudioView speech) const {
    return impl_->analyze(speech);
}

const WorldAcousticAnalysisOptions&
WorldAcousticAnalyzer::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
