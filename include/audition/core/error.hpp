#pragma once

#include <stdexcept>
#include <string>

#include <audition/core/export.hpp>

namespace audition {

enum class ErrorCode {
    InvalidArgument,
    InvalidState,
    UnsupportedFormat,
    UnsupportedCapability,
    BackendUnavailable,
    ConfigurationError,
    ModelLoadError,
    ProcessingError,
    ClockDomainMismatch,
    Cancelled,
};

class AUDITION_API Error : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message);

    [[nodiscard]] ErrorCode code() const noexcept;

private:
    ErrorCode code_;
};

}  // namespace audition
