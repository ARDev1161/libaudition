#pragma once

#include <string>

#include <audition/backends/sherpa/options.hpp>
#include <audition/backend/capabilities.hpp>
#include <audition/speech/types.hpp>

#include <sherpa-onnx/c-api/cxx-api.h>

namespace audition::sherpa_detail {

[[nodiscard]] std::string providerFor(const SherpaRuntimeOptions& runtime);

[[nodiscard]] sherpa_onnx::cxx::OfflineRecognizerConfig makeOfflineAsrConfig(
    const SherpaOfflineAsrOptions& options);
[[nodiscard]] sherpa_onnx::cxx::OnlineRecognizerConfig makeStreamingAsrConfig(
    const SherpaStreamingAsrOptions& options);
[[nodiscard]] sherpa_onnx::cxx::KeywordSpotterConfig makeKeywordSpotterConfig(
    const SherpaKeywordSpotterOptions& options);
[[nodiscard]] sherpa_onnx::cxx::VadModelConfig makeVadConfig(
    const SherpaVadOptions& options);
[[nodiscard]] sherpa_onnx::cxx::SpokenLanguageIdentificationConfig makeLanguageIdConfig(
    const SherpaLanguageIdOptions& options);

[[nodiscard]] AudioRequirements monoRequirements(std::uint32_t sample_rate_hz);
void validateMonoAudio(AudioView audio, std::uint32_t sample_rate_hz, const char* role);

[[nodiscard]] Transcript transcriptFromOffline(
    const sherpa_onnx::cxx::OfflineRecognizerResult& result,
    SpeechSegmentId segment_id);
[[nodiscard]] Transcript transcriptFromOnline(
    const sherpa_onnx::cxx::OnlineRecognizerResult& result);

[[nodiscard]] bool offlineTokenTimestampsConfigured(const SherpaOfflineAsrOptions& options);
[[nodiscard]] bool offlineLanguageIdentificationExpected(const SherpaOfflineAsrOptions& options);

}  // namespace audition::sherpa_detail
