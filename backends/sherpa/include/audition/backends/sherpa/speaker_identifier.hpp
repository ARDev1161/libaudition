#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speaker.hpp>

namespace audition {

class AUDITION_API SherpaSpeakerIdentifier final : public ISpeakerIdentifier {
public:
    explicit SherpaSpeakerIdentifier(SherpaSpeakerIdentifierOptions options);
    ~SherpaSpeakerIdentifier() override;

    SherpaSpeakerIdentifier(const SherpaSpeakerIdentifier&) = delete;
    SherpaSpeakerIdentifier& operator=(const SherpaSpeakerIdentifier&) = delete;
    SherpaSpeakerIdentifier(SherpaSpeakerIdentifier&&) noexcept;
    SherpaSpeakerIdentifier& operator=(SherpaSpeakerIdentifier&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    void clear() override;
    void enroll(const SpeakerEnrollment& enrollment) override;
    [[nodiscard]] bool remove(SpeakerId speaker_id) override;
    [[nodiscard]] std::optional<SpeakerIdentity> identify(
        const SpeakerEmbedding& candidate,
        Score threshold) const override;
    [[nodiscard]] std::vector<SpeakerIdentity> identifyTopK(
        const SpeakerEmbedding& candidate,
        Score threshold,
        std::size_t max_results) const override;

    [[nodiscard]] const SherpaSpeakerIdentifierOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
