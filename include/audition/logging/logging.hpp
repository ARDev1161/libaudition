#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <audition/core/export.hpp>

namespace audition {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off,
};

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void log(LogLevel level, std::string_view component, std::string_view message) = 0;
    virtual void flush() = 0;
};

class AUDITION_API Logger {
public:
    explicit Logger(std::string component);
    Logger(std::string component, std::shared_ptr<ILogSink> sink);

    void log(LogLevel level, std::string_view message) const;
    void trace(std::string_view message) const;
    void debug(std::string_view message) const;
    void info(std::string_view message) const;
    void warning(std::string_view message) const;
    void error(std::string_view message) const;

private:
    std::string component_{};
    std::shared_ptr<ILogSink> sink_{};
};

AUDITION_API void setDefaultLogSink(std::shared_ptr<ILogSink> sink);
AUDITION_API std::shared_ptr<ILogSink> defaultLogSink();

}  // namespace audition
