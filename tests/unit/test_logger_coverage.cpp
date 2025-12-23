#include "simple_test.h"
#include "nexus/utils/Logger.h"
#include <sstream>
#include <iostream>

using namespace Nexus::rpc;

// Helper to capture stdout/stderr
class OutputCapturer {
public:
    OutputCapturer() {
        old_cout = std::cout.rdbuf();
        old_cerr = std::cerr.rdbuf();
        std::cout.rdbuf(cout_ss.rdbuf());
        std::cerr.rdbuf(cerr_ss.rdbuf());
    }

    ~OutputCapturer() {
        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);
    }

    std::string getStdout() { return cout_ss.str(); }
    std::string getStderr() { return cerr_ss.str(); }

private:
    std::streambuf* old_cout;
    std::streambuf* old_cerr;
    std::stringstream cout_ss;
    std::stringstream cerr_ss;
};

TEST(LoggerCoverageTest, LogLevels) {
    auto& logger = Logger::instance();
    OutputCapturer capturer;

    logger.setLevel(Logger::Level::DEBUG);
    logger.setShowTimestamp(false); // Simplify output check
    logger.setShowComponent(true);

    logger.log(Logger::Level::DEBUG, "TEST", "Debug message");
    logger.log(Logger::Level::INFO, "TEST", "Info message");
    logger.log(Logger::Level::WARN, "TEST", "Warn message");
    logger.log(Logger::Level::ERROR, "TEST", "Error message");

    std::string stdout_str = capturer.getStdout();
    std::string stderr_str = capturer.getStderr();

    ASSERT_TRUE(stdout_str.find("[DEBUG] [TEST] Debug message") != std::string::npos);
    ASSERT_TRUE(stdout_str.find("[INFO ] [TEST] Info message") != std::string::npos);
    ASSERT_TRUE(stderr_str.find("[WARN ] [TEST] Warn message") != std::string::npos);
    ASSERT_TRUE(stderr_str.find("[ERROR] [TEST] Error message") != std::string::npos);
}

TEST(LoggerCoverageTest, MinLevelFiltering) {
    auto& logger = Logger::instance();
    OutputCapturer capturer;

    logger.setLevel(Logger::Level::WARN);
    logger.setShowTimestamp(false);

    logger.log(Logger::Level::INFO, "TEST", "Should not appear");
    logger.log(Logger::Level::WARN, "TEST", "Should appear");

    std::string stdout_str = capturer.getStdout();
    std::string stderr_str = capturer.getStderr();

    ASSERT_TRUE(stdout_str.find("Should not appear") == std::string::npos);
    ASSERT_TRUE(stderr_str.find("Should appear") != std::string::npos);
}

TEST(LoggerCoverageTest, ComponentVisibility) {
    auto& logger = Logger::instance();
    OutputCapturer capturer;

    logger.setLevel(Logger::Level::INFO);
    logger.setShowTimestamp(false);
    logger.setShowComponent(false);

    logger.log(Logger::Level::INFO, "TEST", "No component");

    std::string stdout_str = capturer.getStdout();
    ASSERT_TRUE(stdout_str.find("[TEST]") == std::string::npos);
    ASSERT_TRUE(stdout_str.find("No component") != std::string::npos);

    logger.setShowComponent(true);
    logger.log(Logger::Level::INFO, "TEST", "With component");
    stdout_str = capturer.getStdout();
    ASSERT_TRUE(stdout_str.find("[TEST]") != std::string::npos);
}

TEST(LoggerCoverageTest, TimestampVisibility) {
    auto& logger = Logger::instance();
    OutputCapturer capturer;

    logger.setLevel(Logger::Level::INFO);
    logger.setShowTimestamp(true);

    logger.log(Logger::Level::INFO, "TEST", "With timestamp");

    std::string stdout_str = capturer.getStdout();
    // Timestamp format is [YYYY-MM-DD HH:MM:SS.mmm]
    // Just check for the brackets and length roughly
    ASSERT_TRUE(stdout_str.find("[20") != std::string::npos); // Assuming year starts with 20
}

TEST(LoggerCoverageTest, InvalidLevel) {
    auto& logger = Logger::instance();
    OutputCapturer capturer;

    logger.setLevel(Logger::Level::DEBUG);
    logger.setShowTimestamp(false);

    // Cast to Level to bypass type safety for coverage
    logger.log(static_cast<Logger::Level>(100), "TEST", "Invalid level");

    // Invalid level (100) is >= WARN (2), so it goes to stderr
    std::string stderr_str = capturer.getStderr();
    if (stderr_str.find("[?????]") == std::string::npos) {
        // Fallback check in stdout just in case implementation changes
        std::string stdout_str = capturer.getStdout();
        ASSERT_TRUE(stdout_str.find("[?????]") != std::string::npos || stderr_str.find("[?????]") != std::string::npos);
    } else {
        ASSERT_TRUE(stderr_str.find("[?????]") != std::string::npos);
    }
}

TEST(LoggerCoverageTest, Macros) {
    auto& logger = Logger::instance();
    OutputCapturer capturer;
    logger.setLevel(Logger::Level::DEBUG);
    logger.setShowTimestamp(false);

    NEXUS_LOG_DEBUG("MACRO", "Debug macro");
    NEXUS_LOG_INFO("MACRO", "Info macro");
    NEXUS_LOG_WARN("MACRO", "Warn macro");
    NEXUS_LOG_ERROR("MACRO", "Error macro");

    std::string out = capturer.getStdout();
    std::string err = capturer.getStderr();

    ASSERT_TRUE(out.find("Debug macro") != std::string::npos);
    ASSERT_TRUE(out.find("Info macro") != std::string::npos);
    ASSERT_TRUE(err.find("Warn macro") != std::string::npos);
    ASSERT_TRUE(err.find("Error macro") != std::string::npos);
}

TEST(LoggerCoverageTest, StreamMacros) {
    auto& logger = Logger::instance();
    OutputCapturer capturer;
    logger.setLevel(Logger::Level::DEBUG);
    logger.setShowTimestamp(false);

    NEXUS_DEBUG("STREAM") << "Debug " << 1;
    NEXUS_INFO("STREAM") << "Info " << 2;
    NEXUS_WARN("STREAM") << "Warn " << 3;
    NEXUS_ERROR("STREAM") << "Error " << 4;

    std::string out = capturer.getStdout();
    std::string err = capturer.getStderr();

    ASSERT_TRUE(out.find("Debug 1") != std::string::npos);
    ASSERT_TRUE(out.find("Info 2") != std::string::npos);
    ASSERT_TRUE(err.find("Warn 3") != std::string::npos);
    ASSERT_TRUE(err.find("Error 4") != std::string::npos);
}
