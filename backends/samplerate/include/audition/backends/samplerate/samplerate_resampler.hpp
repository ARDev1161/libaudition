#pragma once

#include <memory>

#include <audition/core/export.hpp>
#include <audition/interfaces/resampler.hpp>

namespace audition {

enum class SamplerateConverter {
    SincBest,
    SincMedium,
    SincFast,
    ZeroOrderHold,
    Linear,
};

struct SamplerateOptions {
    SamplerateConverter converter{SamplerateConverter::SincMedium};
};

class AUDITION_API SamplerateResampler final : public IAudioResampler {
public:
    explicit SamplerateResampler(SamplerateOptions options = {});
    ~SamplerateResampler() override;

    SamplerateResampler(const SamplerateResampler&) = delete;
    SamplerateResampler& operator=(const SamplerateResampler&) = delete;
    SamplerateResampler(SamplerateResampler&&) noexcept;
    SamplerateResampler& operator=(SamplerateResampler&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] std::unique_ptr<IAudioResamplerSession> createSession(
        const ResamplerConfig& config) const override;

    [[nodiscard]] const SamplerateOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
