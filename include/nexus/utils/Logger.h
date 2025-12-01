#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace nexus {

/**
 * @brief Unified logging system for Nexus
 */
class Logger {
public:
    enum class Level {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3,
        NONE = 4
    };
    
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
    nexus::Logger::instance().log(nexus::Logger::Level::DEBUG, component, msg)

#define NEXUS_LOG_INFO(component, msg) \
    nexus::Logger::instance().log(nexus::Logger::Level::INFO, component, msg)

#define NEXUS_LOG_WARN(component, msg) \
    nexus::Logger::instance().log(nexus::Logger::Level::WARN, component, msg)

#define NEXUS_LOG_ERROR(component, msg) \
    nexus::Logger::instance().log(nexus::Logger::Level::ERROR, component, msg)

// Stream-style logging
class LogStream {
public:
    LogStream(Logger::Level level, const std::string& component)
        : level_(level), component_(component) {}
    
    ~LogStream() {
        Logger::instance().log(level_, component_, ss_.str());
    }
    
    template<typename T>
    LogStream& operator<<(const T& value) {
        ss_ << value;
        return *this;
    }
    
private:
    Logger::Level level_;
    std::string component_;
    std::stringstream ss_;
};

#define NEXUS_DEBUG(component) \
    nexus::LogStream(nexus::Logger::Level::DEBUG, component)

#define NEXUS_INFO(component) \
    nexus::LogStream(nexus::Logger::Level::INFO, component)

#define NEXUS_WARN(component) \
    nexus::LogStream(nexus::Logger::Level::WARN, component)

#define NEXUS_ERROR(component) \
    nexus::LogStream(nexus::Logger::Level::ERROR, component)

} // namespace nexus
