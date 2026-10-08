#pragma once

#include <memory>

#include <audition/backends/aasist/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/authenticity.hpp>

namespace audition {

class AUDITION_API AasistAuthenticityDetector final
    : public IAudioAuthenticityDetector {
public:
    explicit AasistAuthenticityDetector(AasistOnnxOptions options);
    ~AasistAuthenticityDetector() override;

    AasistAuthenticityDetector(
        const AasistAuthenticityDetector&) = delete;
    AasistAuthenticityDetector& operator=(
        const AasistAuthenticityDetector&) = delete;
    AasistAuthenticityDetector(
        AasistAuthenticityDetector&&) noexcept;
    AasistAuthenticityDetector& operator=(
        AasistAuthenticityDetector&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AuthenticityCapabilities capabilities() const override;
    [[nodiscard]] AuthenticityResult analyze(
        AudioView speech) const override;

    [[nodiscard]] const AasistOnnxOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
