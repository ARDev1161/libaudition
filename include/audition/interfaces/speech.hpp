#pragma once

#include <memory>
#include <string>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/backend/capabilities.hpp>
#include <audition/spatial/types.hpp>
#include <audition/speech/types.hpp>

namespace audition {

class IVadSession {
public:
    virtual ~IVadSession() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual VadResult process(AudioView audio) = 0;
};

class IVoiceActivityDetector {
public:
    virtual ~IVoiceActivityDetector() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AudioRequirements audioRequirements() const = 0;
    [[nodiscard]] virtual std::unique_ptr<IVadSession> createSession() const = 0;
};

struct SpeechSegmentationOptions {
    Duration pre_roll{Duration::fromChrono(std::chrono::milliseconds{200})};
    Duration post_roll{Duration::fromChrono(std::chrono::milliseconds{300})};
    Duration minimum_speech{Duration::fromChrono(std::chrono::milliseconds{100})};
};

class ISpeechSegmenterSession {
public:
    virtual ~ISpeechSegmenterSession() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual std::vector<SpeechSegment> process(
        const TrackedAudioFrame& frame, const VadResult& vad) = 0;
    [[nodiscard]] virtual std::vector<SpeechSegment> flush() = 0;
};

class ISpeechSegmenter {
public:
    virtual ~ISpeechSegmenter() = default;
    [[nodiscard]] virtual std::unique_ptr<ISpeechSegmenterSession> createSession(
        SpeechSegmentationOptions options) const = 0;
};

class IAsrEngine {
public:
    virtual ~IAsrEngine() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AsrCapabilities capabilities() const = 0;
    [[nodiscard]] virtual Transcript transcribe(const SpeechSegment& segment) const = 0;
};

class IStreamingAsrSession {
public:
    virtual ~IStreamingAsrSession() = default;
    virtual void reset() = 0;
    virtual void accept(AudioView audio) = 0;
    [[nodiscard]] virtual Transcript partial() const = 0;
    [[nodiscard]] virtual bool endpointDetected() const = 0;
    [[nodiscard]] virtual Transcript finalize() = 0;
};

class IStreamingAsrEngine {
public:
    virtual ~IStreamingAsrEngine() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AsrCapabilities capabilities() const = 0;
    [[nodiscard]] virtual std::unique_ptr<IStreamingAsrSession> createSession() const = 0;
};

class IKeywordSpotterSession {
public:
    virtual ~IKeywordSpotterSession() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual std::vector<KeywordHit> process(AudioView audio) = 0;
};

class IKeywordSpotter {
public:
    virtual ~IKeywordSpotter() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AudioRequirements audioRequirements() const = 0;
    [[nodiscard]] virtual std::unique_ptr<IKeywordSpotterSession> createSession() const = 0;
};

struct LanguageScore {
    std::string language{};
    std::optional<Probability> probability{};
};

class ILanguageIdentifier {
public:
    virtual ~ILanguageIdentifier() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AudioRequirements audioRequirements() const = 0;
    [[nodiscard]] virtual std::vector<LanguageScore> identify(AudioView speech) const = 0;
};

}  // namespace audition
