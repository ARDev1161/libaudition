#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speaker.hpp>

namespace audition {

class AUDITION_API SherpaSpeakerVerifier final : public ISpeakerVerifier {
public:
    explicit SherpaSpeakerVerifier(SherpaSpeakerVerifierOptions options = {});
    ~SherpaSpeakerVerifier() override;

    SherpaSpeakerVerifier(const SherpaSpeakerVerifier&) = delete;
    SherpaSpeakerVerifier& operator=(const SherpaSpeakerVerifier&) = delete;
    SherpaSpeakerVerifier(SherpaSpeakerVerifier&&) noexcept;
    SherpaSpeakerVerifier& operator=(SherpaSpeakerVerifier&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] SpeakerVerificationResult compare(
        const SpeakerEmbedding& reference,
        const SpeakerEmbedding& candidate,
        Score threshold) const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
