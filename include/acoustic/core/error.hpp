#pragma once

#include <stdexcept>
#include <string>

#include <acoustic/core/export.hpp>

namespace acoustic {

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

class ACOUSTIC_API Error : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message);

    [[nodiscard]] ErrorCode code() const noexcept;

private:
    ErrorCode code_;
};

}  // namespace acoustic
