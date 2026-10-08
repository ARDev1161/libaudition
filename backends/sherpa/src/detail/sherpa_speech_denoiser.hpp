#pragma once

#include <cstdint>

#include <audition/audio/audio_buffer.hpp>
#include <audition/backends/sherpa/options.hpp>

#include <sherpa-onnx/c-api/cxx-api.h>

namespace audition::sherpa_detail {

[[nodiscard]] sherpa_onnx::cxx::OfflineSpeechDenoiserConfig
makeOfflineSpeechDenoiserConfig(const SherpaSpeechDenoiserOptions& options);

[[nodiscard]] sherpa_onnx::cxx::OnlineSpeechDenoiserConfig
makeStreamingSpeechDenoiserConfig(const SherpaSpeechDenoiserOptions& options);

void validateDenoiserFormat(const AudioFormat& format,
                            std::uint32_t expected_sample_rate_hz,
                            const char* role);

void validateDenoiserInput(AudioView audio,
                           const AudioFormat& session_format,
                           const char* role);

[[nodiscard]] AudioBuffer denoisedAudioBuffer(
    sherpa_onnx::cxx::DenoisedAudio output,
    const AudioFormat& format,
    Timestamp capture_time,
    std::uint64_t sequence_number,
    const char* role);

}  // namespace audition::sherpa_detail
