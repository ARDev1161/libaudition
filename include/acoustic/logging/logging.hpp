#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <acoustic/core/export.hpp>

namespace acoustic {

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

class ACOUSTIC_API Logger {
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

ACOUSTIC_API void setDefaultLogSink(std::shared_ptr<ILogSink> sink);
ACOUSTIC_API std::shared_ptr<ILogSink> defaultLogSink();

}  // namespace acoustic
