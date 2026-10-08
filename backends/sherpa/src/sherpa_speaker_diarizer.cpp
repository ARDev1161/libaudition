#include <audition/backends/sherpa/speaker_diarizer.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"
#include "detail/sherpa_speaker_config.hpp"

namespace audition {
namespace {

Duration secondsToDuration(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0F) {
        throw Error{ErrorCode::ProcessingError,
                    "Sherpa diarization returned an invalid timestamp"};
    }
    return Duration{static_cast<std::int64_t>(
        std::llround(static_cast<double>(seconds) * 1.0e9))};
}

}  // namespace

class SherpaSpeakerDiarizer::Impl {
public:
    explicit Impl(SherpaSpeakerDiarizationOptions options)
        : options_(std::move(options)),
          diarizer_(sherpa_onnx::cxx::OfflineSpeakerDiarization::Create(
              sherpa_detail::makeSpeakerDiarizationConfig(options_))) {
        if (!diarizer_.Get()) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create Sherpa offline speaker diarizer"};
        }
        const auto sample_rate = diarizer_.GetSampleRate();
        if (sample_rate <= 0) {
            throw Error{ErrorCode::ModelLoadError,
                        "Sherpa diarizer returned an invalid sample rate"};
        }
        sample_rate_hz_ = static_cast<std::uint32_t>(sample_rate);
    }

    [[nodiscard]] AudioRequirements requirements() const {
        return sherpa_detail::monoRequirements(sample_rate_hz_);
    }

    [[nodiscard]] SpeakerDiarizationResult diarize(AudioView audio) const {
        sherpa_detail::validateMonoAudio(
            audio, sample_rate_hz_, "speaker diarization");
        if (audio.sampleCount() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Speaker diarization input exceeds backend range"};
        }

        std::lock_guard<std::mutex> lock{mutex_};
        const auto native = diarizer_.Process(
            audio.data(), static_cast<std::int32_t>(audio.sampleCount()));

        SpeakerDiarizationResult result{};
        result.segments.reserve(native.size());
        std::unordered_set<std::uint32_t> speakers;

        for (const auto& segment : native) {
            if (segment.speaker < 0) {
                throw Error{ErrorCode::ProcessingError,
                            "Sherpa diarization returned a negative speaker label"};
            }
            SpeakerDiarizationSegment output{};
            output.speaker_index = static_cast<std::uint32_t>(segment.speaker);
            output.start_offset = secondsToDuration(segment.start);
            output.end_offset = secondsToDuration(segment.end);
            if (output.end_offset < output.start_offset) {
                throw Error{ErrorCode::ProcessingError,
                            "Sherpa diarization returned an inverted segment"};
            }
            if (segment.confidence !=
                sherpa_onnx::cxx::OfflineSpeakerDiarizationSegment::
                    kUnavailableConfidence) {
                if (!std::isfinite(segment.confidence) ||
                    segment.confidence < -1.0F ||
                    segment.confidence > 1.0F) {
                    throw Error{ErrorCode::ProcessingError,
                                "Sherpa diarization returned invalid confidence"};
                }
                output.confidence = Score{static_cast<double>(segment.confidence)};
            }
            speakers.insert(output.speaker_index);
            result.segments.push_back(std::move(output));
        }

        result.speaker_count = static_cast<std::uint32_t>(speakers.size());
        return result;
    }

    SherpaSpeakerDiarizationOptions options_{};
    sherpa_onnx::cxx::OfflineSpeakerDiarization diarizer_;
    std::uint32_t sample_rate_hz_{0};
    mutable std::mutex mutex_{};
};

SherpaSpeakerDiarizer::SherpaSpeakerDiarizer(
    SherpaSpeakerDiarizationOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaSpeakerDiarizer::~SherpaSpeakerDiarizer() = default;
SherpaSpeakerDiarizer::SherpaSpeakerDiarizer(SherpaSpeakerDiarizer&&) noexcept = default;
SherpaSpeakerDiarizer& SherpaSpeakerDiarizer::operator=(
    SherpaSpeakerDiarizer&&) noexcept = default;

BackendInfo SherpaSpeakerDiarizer::backendInfo() const {
    return {"sherpa-onnx-speaker-diarization", "99ddefaa"};
}
AudioRequirements SherpaSpeakerDiarizer::audioRequirements() const {
    return impl_->requirements();
}
SpeakerDiarizationResult SherpaSpeakerDiarizer::diarize(AudioView audio) const {
    return impl_->diarize(audio);
}
const SherpaSpeakerDiarizationOptions& SherpaSpeakerDiarizer::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition
