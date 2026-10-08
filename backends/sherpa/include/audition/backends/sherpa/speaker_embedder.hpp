#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speaker.hpp>

namespace audition {

class AUDITION_API SherpaSpeakerEmbedder final : public ISpeakerEmbedder {
public:
    explicit SherpaSpeakerEmbedder(SherpaSpeakerEmbeddingOptions options);
    ~SherpaSpeakerEmbedder() override;

    SherpaSpeakerEmbedder(const SherpaSpeakerEmbedder&) = delete;
    SherpaSpeakerEmbedder& operator=(const SherpaSpeakerEmbedder&) = delete;
    SherpaSpeakerEmbedder(SherpaSpeakerEmbedder&&) noexcept;
    SherpaSpeakerEmbedder& operator=(SherpaSpeakerEmbedder&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AudioRequirements audioRequirements() const override;
    [[nodiscard]] std::size_t embeddingDimension() const override;
    [[nodiscard]] std::optional<SpeakerEmbedding> embed(AudioView speech) const override;

    [[nodiscard]] const SherpaSpeakerEmbeddingOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
