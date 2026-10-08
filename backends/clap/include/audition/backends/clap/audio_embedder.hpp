#pragma once

#include <memory>

#include <audition/backends/clap/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/classify.hpp>

namespace audition {

class AUDITION_API ClapAudioEmbedder final : public IAudioEmbedder {
public:
    explicit ClapAudioEmbedder(ClapOnnxAudioOptions options);
    ~ClapAudioEmbedder() override;

    ClapAudioEmbedder(const ClapAudioEmbedder&) = delete;
    ClapAudioEmbedder& operator=(const ClapAudioEmbedder&) = delete;
    ClapAudioEmbedder(ClapAudioEmbedder&&) noexcept;
    ClapAudioEmbedder& operator=(ClapAudioEmbedder&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] EmbeddingCapabilities capabilities() const override;
    [[nodiscard]] AudioEmbedding embed(AudioView audio) const override;

    [[nodiscard]] const ClapOnnxAudioOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
