#pragma once

#include <memory>

#include <audition/core/export.hpp>
#include <audition/interfaces/audio.hpp>

namespace audition {

struct WebRtcAec3Options {
    double filter_initial_state_seconds{0.5};
    bool conservative_initial_phase{false};
};

class AUDITION_API WebRtcAec3EchoCanceller final : public IEchoCanceller {
public:
    explicit WebRtcAec3EchoCanceller(WebRtcAec3Options options = {});
    ~WebRtcAec3EchoCanceller() override;

    WebRtcAec3EchoCanceller(const WebRtcAec3EchoCanceller&) = delete;
    WebRtcAec3EchoCanceller& operator=(const WebRtcAec3EchoCanceller&) = delete;
    WebRtcAec3EchoCanceller(WebRtcAec3EchoCanceller&&) noexcept;
    WebRtcAec3EchoCanceller& operator=(WebRtcAec3EchoCanceller&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] std::unique_ptr<IEchoCancellerSession> createSession(
        const AudioFormat& capture_format,
        const AudioFormat& reference_format) const override;

    [[nodiscard]] const WebRtcAec3Options& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
