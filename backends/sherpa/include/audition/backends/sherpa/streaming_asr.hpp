#pragma once

#include <memory>

#include <audition/backends/sherpa/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/speech.hpp>

namespace audition {

class AUDITION_API SherpaStreamingAsr final : public IStreamingAsrEngine {
public:
    explicit SherpaStreamingAsr(SherpaStreamingAsrOptions options);
    ~SherpaStreamingAsr() override;
    SherpaStreamingAsr(const SherpaStreamingAsr&) = delete;
    SherpaStreamingAsr& operator=(const SherpaStreamingAsr&) = delete;
    SherpaStreamingAsr(SherpaStreamingAsr&&) noexcept;
    SherpaStreamingAsr& operator=(SherpaStreamingAsr&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] AsrCapabilities capabilities() const override;
    [[nodiscard]] std::unique_ptr<IStreamingAsrSession> createSession() const override;
    [[nodiscard]] const SherpaStreamingAsrOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
