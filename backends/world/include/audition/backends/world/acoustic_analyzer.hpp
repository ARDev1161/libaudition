#pragma once

#include <memory>

#include <audition/backends/world/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/voice.hpp>

namespace audition {

class AUDITION_API WorldAcousticAnalyzer final
    : public IVoiceAcousticAnalyzer {
public:
    explicit WorldAcousticAnalyzer(
        WorldAcousticAnalysisOptions options = {});
    ~WorldAcousticAnalyzer() override;

    WorldAcousticAnalyzer(
        const WorldAcousticAnalyzer&) = delete;
    WorldAcousticAnalyzer& operator=(
        const WorldAcousticAnalyzer&) = delete;
    WorldAcousticAnalyzer(
        WorldAcousticAnalyzer&&) noexcept;
    WorldAcousticAnalyzer& operator=(
        WorldAcousticAnalyzer&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] VoiceAcousticCapabilities capabilities() const override;
    [[nodiscard]] VoiceAcousticFeatures analyze(
        AudioView speech) const override;

    [[nodiscard]] const WorldAcousticAnalysisOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
