#pragma once

#include <memory>
#include <string>

#include <audition/logging/logging.hpp>

namespace audition {

struct SpdlogSinkOptions {
    LogLevel level{LogLevel::Info};
    std::string pattern{"[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v"};
    bool use_stderr{false};
    bool colored{true};
};

/**
 * @brief Creates a synchronous spdlog-backed sink.
 *
 * No logging thread is created. Applications that require asynchronous logging
 * should provide their own ILogSink or a future explicit async adapter.
 */
[[nodiscard]] std::shared_ptr<ILogSink> makeSpdlogSink(const SpdlogSinkOptions& options = {});

}  // namespace audition
