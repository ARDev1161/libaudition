#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>

namespace audition {

class AUDITION_API SherpaOfflineAsr final : public IAsrEngine {
public:
    explicit SherpaOfflineAsr(SherpaOfflineAsrOptions options);
    ~SherpaOfflineAsr() override;
    SherpaOfflineAsr(const SherpaOfflineAsr&) = delete;
    SherpaOfflineAsr& operator=(const SherpaOfflineAsr&) = delete;
    SherpaOfflineAsr(SherpaOfflineAsr&&) noexcept;
    SherpaOfflineAsr& operator=(SherpaOfflineAsr&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AsrCapabilities capabilities() const override;
    [[nodiscard]] Transcript transcribe(const SpeechSegment& segment) const override;
    [[nodiscard]] const SherpaOfflineAsrOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
