#include <audition/logging/logging.hpp>

#include <mutex>
#include <utility>

namespace audition {
namespace {

class NullLogSink final : public ILogSink {
public:
    void log(LogLevel, std::string_view, std::string_view) override {}
    void flush() override {}
};

std::mutex& sinkMutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<ILogSink>& sinkStorage() {
    static std::shared_ptr<ILogSink> sink = std::make_shared<NullLogSink>();
    return sink;
}

}  // namespace

Logger::Logger(std::string component) : Logger(std::move(component), defaultLogSink()) {}

Logger::Logger(std::string component, std::shared_ptr<ILogSink> sink)
    : component_(std::move(component)), sink_(std::move(sink)) {
    if (!sink_) {
        sink_ = defaultLogSink();
    }
}

void Logger::log(LogLevel level, std::string_view message) const {
    if (sink_ && level != LogLevel::Off) {
        sink_->log(level, component_, message);
    }
}
void Logger::trace(std::string_view message) const { log(LogLevel::Trace, message); }
void Logger::debug(std::string_view message) const { log(LogLevel::Debug, message); }
void Logger::info(std::string_view message) const { log(LogLevel::Info, message); }
void Logger::warning(std::string_view message) const { log(LogLevel::Warning, message); }
void Logger::error(std::string_view message) const { log(LogLevel::Error, message); }

void setDefaultLogSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard<std::mutex> lock{sinkMutex()};
    sinkStorage() = sink ? std::move(sink) : std::make_shared<NullLogSink>();
}

std::shared_ptr<ILogSink> defaultLogSink() {
    std::lock_guard<std::mutex> lock{sinkMutex()};
    return sinkStorage();
}

}  // namespace audition
