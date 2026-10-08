#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/classify.hpp>

namespace audition {

class AUDITION_API SherpaAudioTagger final : public IAudioClassifier {
public:
    explicit SherpaAudioTagger(SherpaAudioTaggingOptions options);
    ~SherpaAudioTagger() override;

    SherpaAudioTagger(const SherpaAudioTagger&) = delete;
    SherpaAudioTagger& operator=(const SherpaAudioTagger&) = delete;
    SherpaAudioTagger(SherpaAudioTagger&&) noexcept;
    SherpaAudioTagger& operator=(SherpaAudioTagger&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] ClassifierCapabilities capabilities() const override;
    [[nodiscard]] ClassificationResult classify(AudioView audio) const override;

    [[nodiscard]] const SherpaAudioTaggingOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
