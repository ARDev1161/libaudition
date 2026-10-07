#include <audition/backends/spdlog/spdlog_sink.hpp>

#include <memory>
#include <string>

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace audition {
namespace {

spdlog::level::level_enum toSpdlogLevel(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warning:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    case LogLevel::Off:
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

class SpdlogSink final : public ILogSink {
public:
    explicit SpdlogSink(const SpdlogSinkOptions& options) {
        std::shared_ptr<spdlog::sinks::sink> sink;
        if (options.colored) {
            sink = options.use_stderr
                       ? std::static_pointer_cast<spdlog::sinks::sink>(
                             std::make_shared<spdlog::sinks::stderr_color_sink_mt>())
                       : std::static_pointer_cast<spdlog::sinks::sink>(
                             std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        } else {
            sink = options.use_stderr
                       ? std::static_pointer_cast<spdlog::sinks::sink>(
                             std::make_shared<spdlog::sinks::stderr_sink_mt>())
                       : std::static_pointer_cast<spdlog::sinks::sink>(
                             std::make_shared<spdlog::sinks::stdout_sink_mt>());
        }
        logger_ = std::make_shared<spdlog::logger>("libaudition", std::move(sink));
        logger_->set_level(toSpdlogLevel(options.level));
        logger_->set_pattern(options.pattern);
    }

    void log(LogLevel level, std::string_view component, std::string_view message) override {
        logger_->log(toSpdlogLevel(level), "[{}] {}", component, message);
    }

    void flush() override { logger_->flush(); }

private:
    std::shared_ptr<spdlog::logger> logger_{};
};

}  // namespace

std::shared_ptr<ILogSink> makeSpdlogSink(const SpdlogSinkOptions& options) {
    return std::make_shared<SpdlogSink>(options);
}

}  // namespace audition
