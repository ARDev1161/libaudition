#pragma once

#include <string>
#include <vector>

#include <audition/audio/audio_buffer.hpp>
#include <audition/backend/capabilities.hpp>
#include <audition/classify/types.hpp>

namespace audition {

class IAudioClassifier {
public:
    virtual ~IAudioClassifier() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual ClassifierCapabilities capabilities() const = 0;
    [[nodiscard]] virtual ClassificationResult classify(AudioView audio) const = 0;
};

class IOpenVocabularyAudioClassifier {
public:
    virtual ~IOpenVocabularyAudioClassifier() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual ClassificationResult classify(
        AudioView audio, const std::vector<std::string>& candidate_labels) const = 0;
};

class IAudioEmbedder {
public:
    virtual ~IAudioEmbedder() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AudioEmbedding embed(AudioView audio) const = 0;
};

}  // namespace audition
