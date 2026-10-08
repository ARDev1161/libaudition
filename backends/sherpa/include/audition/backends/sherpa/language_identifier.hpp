#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>

namespace audition {

class AUDITION_API SherpaLanguageIdentifier final : public ILanguageIdentifier {
public:
    explicit SherpaLanguageIdentifier(SherpaLanguageIdOptions options);
    ~SherpaLanguageIdentifier() override;
    SherpaLanguageIdentifier(const SherpaLanguageIdentifier&) = delete;
    SherpaLanguageIdentifier& operator=(const SherpaLanguageIdentifier&) = delete;
    SherpaLanguageIdentifier(SherpaLanguageIdentifier&&) noexcept;
    SherpaLanguageIdentifier& operator=(SherpaLanguageIdentifier&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AudioRequirements audioRequirements() const override;
    [[nodiscard]] std::vector<LanguageScore> identify(AudioView speech) const override;
    [[nodiscard]] const SherpaLanguageIdOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
