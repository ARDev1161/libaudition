#include <acoustic/core/error.hpp>

#include <utility>

namespace acoustic {

Error::Error(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

ErrorCode Error::code() const noexcept { return code_; }

}  // namespace acoustic
