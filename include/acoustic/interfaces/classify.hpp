#pragma once

#include <string>
#include <vector>

#include <acoustic/audio/audio_buffer.hpp>
#include <acoustic/backend/capabilities.hpp>
#include <acoustic/classify/types.hpp>

namespace acoustic {

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

}  // namespace acoustic
