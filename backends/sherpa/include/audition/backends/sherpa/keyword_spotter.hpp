#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>

namespace audition {

class AUDITION_API SherpaKeywordSpotter final : public IKeywordSpotter {
public:
    explicit SherpaKeywordSpotter(SherpaKeywordSpotterOptions options);
    ~SherpaKeywordSpotter() override;
    SherpaKeywordSpotter(const SherpaKeywordSpotter&) = delete;
    SherpaKeywordSpotter& operator=(const SherpaKeywordSpotter&) = delete;
    SherpaKeywordSpotter(SherpaKeywordSpotter&&) noexcept;
    SherpaKeywordSpotter& operator=(SherpaKeywordSpotter&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AudioRequirements audioRequirements() const override;
    [[nodiscard]] std::unique_ptr<IKeywordSpotterSession> createSession() const override;
    [[nodiscard]] const SherpaKeywordSpotterOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
