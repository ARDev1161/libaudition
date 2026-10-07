#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <audition/audio/audio_format.hpp>
#include <audition/core/error.hpp>
#include <audition/core/span.hpp>
#include <audition/core/time.hpp>

namespace audition {

class AudioView {
public:
    AudioView() = default;
    AudioView(const float* data, std::size_t sample_count, AudioFormat format,
              Timestamp capture_time, std::uint64_t sequence_number = 0);

    [[nodiscard]] const float* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t sampleCount() const noexcept { return sample_count_; }
    [[nodiscard]] std::size_t frameCount() const noexcept;
    [[nodiscard]] AudioFormat format() const noexcept { return format_; }
    [[nodiscard]] Timestamp captureTime() const noexcept { return capture_time_; }
    [[nodiscard]] std::uint64_t sequenceNumber() const noexcept { return sequence_number_; }
    [[nodiscard]] Duration duration() const noexcept;
    [[nodiscard]] StridedSpan<const float> channel(std::size_t channel_index) const;

private:
    const float* data_{nullptr};
    std::size_t sample_count_{0};
    AudioFormat format_{};
    Timestamp capture_time_{};
    std::uint64_t sequence_number_{0};
};

class AudioBuffer {
public:
    AudioBuffer() = default;
    AudioBuffer(std::vector<float> samples, AudioFormat format, Timestamp capture_time,
                std::uint64_t sequence_number = 0);

    [[nodiscard]] const std::vector<float>& samples() const noexcept { return samples_; }
    [[nodiscard]] Span<float> mutableSamples() noexcept { return Span<float>{samples_.data(), samples_.size()}; }
    [[nodiscard]] AudioFormat format() const noexcept { return format_; }
    [[nodiscard]] Timestamp captureTime() const noexcept { return capture_time_; }
    [[nodiscard]] std::uint64_t sequenceNumber() const noexcept { return sequence_number_; }
    [[nodiscard]] std::size_t frameCount() const noexcept;
    [[nodiscard]] Duration duration() const noexcept;
    [[nodiscard]] AudioView view() const;
    [[nodiscard]] StridedSpan<const float> channel(std::size_t channel_index) const;

private:
    std::vector<float> samples_{};
    AudioFormat format_{};
    Timestamp capture_time_{};
    std::uint64_t sequence_number_{0};
};

}  // namespace audition
