#include <audition/core/error.hpp>

#include <utility>

namespace audition {

Error::Error(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

ErrorCode Error::code() const noexcept { return code_; }

}  // namespace audition
