#include <audition/logging/logging.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {
class RecordingSink final : public audition::ILogSink {
public:
    void log(audition::LogLevel level, std::string_view component,
             std::string_view message) override {
        levels.push_back(level);
        components.emplace_back(component);
        messages.emplace_back(message);
    }
    void flush() override { ++flush_count; }
    std::vector<audition::LogLevel> levels;
    std::vector<std::string> components;
    std::vector<std::string> messages;
    int flush_count{0};
};

struct RestoreDefaultSink {
    std::shared_ptr<audition::ILogSink> original{audition::defaultLogSink()};
    ~RestoreDefaultSink() { audition::setDefaultLogSink(original); }
};
} // namespace

TEST(Logging, RoutesAllLevelsAndSuppressesOff) {
    auto sink = std::make_shared<RecordingSink>();
    audition::Logger logger{"dsp", sink};
    logger.trace("trace");
    logger.debug("debug");
    logger.info("info");
    logger.warning("warn");
    logger.error("error");
    logger.log(audition::LogLevel::Critical, "critical");
    logger.log(audition::LogLevel::Off, "hidden");
    ASSERT_EQ(sink->messages.size(), 6U);
    EXPECT_EQ(sink->messages[0], "trace");
    EXPECT_EQ(sink->messages[5], "critical");
    EXPECT_EQ(sink->levels[5], audition::LogLevel::Critical);
    for (const auto& component : sink->components) EXPECT_EQ(component, "dsp");
    sink->flush();
    EXPECT_EQ(sink->flush_count, 1);
}

TEST(Logging, DefaultSinkAndNullFallbackAreValid) {
    RestoreDefaultSink restore;
    auto sink = std::make_shared<RecordingSink>();
    audition::setDefaultLogSink(sink);
    audition::Logger first{"pipeline"};
    audition::Logger second{"spatial", nullptr};
    first.info("hello");
    second.error("bad");
    ASSERT_EQ(sink->messages.size(), 2U);
    EXPECT_EQ(sink->components[0], "pipeline");
    EXPECT_EQ(sink->components[1], "spatial");
    audition::setDefaultLogSink(nullptr);
    EXPECT_NE(audition::defaultLogSink(), nullptr);
    audition::Logger silent{"quiet"};
    EXPECT_NO_THROW(silent.warning("discarded"));
    EXPECT_EQ(sink->messages.size(), 2U);
}
