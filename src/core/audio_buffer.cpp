#include <audition/audio/audio_buffer.hpp>

#include <limits>

namespace audition {
namespace {

void validateAudio(std::size_t sample_count, const AudioFormat& format) {
    if (!format.valid()) {
        throw Error{ErrorCode::InvalidArgument, "Audio format must have non-zero sample rate and channels"};
    }
    if (sample_count % format.channel_count != 0U) {
        throw Error{ErrorCode::InvalidArgument, "Audio sample count must be divisible by channel count"};
    }
}

Duration audioDuration(std::size_t frames, std::uint32_t sample_rate_hz) noexcept {
    if (sample_rate_hz == 0U) {
        return Duration{};
    }
    const long double ns = static_cast<long double>(frames) * 1'000'000'000.0L /
                           static_cast<long double>(sample_rate_hz);
    return Duration{static_cast<std::int64_t>(ns)};
}

}  // namespace

AudioView::AudioView(const float* data, std::size_t sample_count, AudioFormat format,
                     Timestamp capture_time, std::uint64_t sequence_number)
    : data_(data), sample_count_(sample_count), format_(format), capture_time_(capture_time),
      sequence_number_(sequence_number) {
    validateAudio(sample_count_, format_);
    if (sample_count_ > 0U && data_ == nullptr) {
        throw Error{ErrorCode::InvalidArgument, "Non-empty AudioView requires non-null data"};
    }
}

std::size_t AudioView::frameCount() const noexcept {
    return format_.channel_count == 0U ? 0U : sample_count_ / format_.channel_count;
}

Duration AudioView::duration() const noexcept { return audioDuration(frameCount(), format_.sample_rate_hz); }

StridedSpan<const float> AudioView::channel(std::size_t channel_index) const {
    if (channel_index >= format_.channel_count) {
        throw Error{ErrorCode::InvalidArgument, "Audio channel index is out of range"};
    }
    const auto frames = frameCount();
    if (frames == 0U) {
        return StridedSpan<const float>{nullptr, 0U, 1};
    }
    if (format_.layout == AudioLayout::Interleaved) {
        return StridedSpan<const float>{data_ + channel_index, frames,
                                        static_cast<std::ptrdiff_t>(format_.channel_count)};
    }
    return StridedSpan<const float>{data_ + channel_index * frames, frames, 1};
}

AudioBuffer::AudioBuffer(std::vector<float> samples, AudioFormat format, Timestamp capture_time,
                         std::uint64_t sequence_number)
    : samples_(std::move(samples)), format_(format), capture_time_(capture_time),
      sequence_number_(sequence_number) {
    validateAudio(samples_.size(), format_);
}

std::size_t AudioBuffer::frameCount() const noexcept {
    return format_.channel_count == 0U ? 0U : samples_.size() / format_.channel_count;
}

Duration AudioBuffer::duration() const noexcept { return audioDuration(frameCount(), format_.sample_rate_hz); }

AudioView AudioBuffer::view() const {
    // Construction cannot fail because AudioBuffer validated the invariant.
    return AudioView{samples_.data(), samples_.size(), format_, capture_time_, sequence_number_};
}

StridedSpan<const float> AudioBuffer::channel(std::size_t channel_index) const {
    return view().channel(channel_index);
}

}  // namespace audition
