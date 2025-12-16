#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace Nexus {
namespace rpc {

/**
 * @brief Unified logging system for Nexus
 */
class Logger {
public:
    enum class Level { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, NONE = 4 };

    /**
     * @brief Get global logger instance
     */
    static Logger& instance();

    /**
     * @brief Set minimum log level
     */
    void setLevel(Level level);

    /**
     * @brief Get current log level
     */
    Level getLevel() const;

    /**
     * @brief Log a message
     */
    void log(Level level, const std::string& component, const std::string& message);

    /**
     * @brief Enable/disable timestamp
     */
    void setShowTimestamp(bool show);

    /**
     * @brief Enable/disable component name
     */
    void setShowComponent(bool show);

private:
    Logger();

    Level min_level_{Level::INFO};
    bool show_timestamp_{true};
    bool show_component_{true};
    mutable std::mutex mutex_;

    const char* levelToString(Level level) const;
    std::string getTimestamp() const;
};

// Convenience macros
#define NEXUS_LOG_DEBUG(component, msg) \
    Nexus::rpc::Logger::instance().log(Nexus::rpc::Logger::Level::DEBUG, component, msg)

#define NEXUS_LOG_INFO(component, msg) \
    Nexus::rpc::Logger::instance().log(Nexus::rpc::Logger::Level::INFO, component, msg)

#define NEXUS_LOG_WARN(component, msg) \
    Nexus::rpc::Logger::instance().log(Nexus::rpc::Logger::Level::WARN, component, msg)

#define NEXUS_LOG_ERROR(component, msg) \
    Nexus::rpc::Logger::instance().log(Nexus::rpc::Logger::Level::ERROR, component, msg)

// Stream-style logging
class LogStream {
public:
    LogStream(Logger::Level level, const std::string& component) : level_(level), component_(component) {}

    ~LogStream() { Logger::instance().log(level_, component_, ss_.str()); }

    template <typename T>
    LogStream& operator<<(const T& value) {
        ss_ << value;
        return *this;
    }

private:
    Logger::Level level_;
    std::string component_;
    std::stringstream ss_;
};

#define NEXUS_DEBUG(component) Nexus::rpc::LogStream(Nexus::rpc::Logger::Level::DEBUG, component)

#define NEXUS_INFO(component) Nexus::rpc::LogStream(Nexus::rpc::Logger::Level::INFO, component)

#define NEXUS_WARN(component) Nexus::rpc::LogStream(Nexus::rpc::Logger::Level::WARN, component)

#define NEXUS_ERROR(component) Nexus::rpc::LogStream(Nexus::rpc::Logger::Level::ERROR, component)

}  // namespace rpc
}  // namespace Nexus
