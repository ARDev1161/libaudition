#pragma once

#include <memory>
#include <optional>

#include <audition/audio/audio_buffer.hpp>
#include <audition/backend/capabilities.hpp>

namespace audition {

class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual AudioFormat format() const = 0;
    virtual bool read(AudioBuffer& destination) = 0;
};

struct EchoCancellationMetrics {
    std::optional<double> echo_return_loss_db{};
    std::optional<double> echo_return_loss_enhancement_db{};
    std::optional<Duration> estimated_delay{};
};

class IEchoCancellerSession {
public:
    virtual ~IEchoCancellerSession() = default;
    virtual void reset() = 0;
    virtual void acceptReference(AudioView playback) = 0;
    virtual void setStreamDelay(Duration delay) = 0;
    virtual void notifyEchoPathChange() = 0;
    [[nodiscard]] virtual AudioBuffer process(AudioView captured) = 0;
    [[nodiscard]] virtual EchoCancellationMetrics metrics() const = 0;
};

class IEchoCanceller {
public:
    virtual ~IEchoCanceller() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual std::unique_ptr<IEchoCancellerSession> createSession(
        const AudioFormat& capture_format, const AudioFormat& reference_format) const = 0;
};

class INoiseSuppressorSession {
public:
    virtual ~INoiseSuppressorSession() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual AudioBuffer process(AudioView input) = 0;
};

class INoiseSuppressor {
public:
    virtual ~INoiseSuppressor() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual std::unique_ptr<INoiseSuppressorSession> createSession(
        const AudioFormat& format) const = 0;
};

}  // namespace audition
