#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>

namespace audition {

class AUDITION_API SherpaVad final : public IVoiceActivityDetector {
public:
    explicit SherpaVad(SherpaVadOptions options);
    ~SherpaVad() override;
    SherpaVad(const SherpaVad&) = delete;
    SherpaVad& operator=(const SherpaVad&) = delete;
    SherpaVad(SherpaVad&&) noexcept;
    SherpaVad& operator=(SherpaVad&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AudioRequirements audioRequirements() const override;
    [[nodiscard]] std::unique_ptr<IVadSession> createSession() const override;
    [[nodiscard]] const SherpaVadOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
