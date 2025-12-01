#include "nexus/utils/Logger.h"
#include <ctime>

namespace Nexus {
namespace rpc {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    // Check environment for log level
    if (const char* level = std::getenv("NEXUS_LOG_LEVEL")) {
        std::string level_str(level);
        if (level_str == "DEBUG") min_level_ = Level::DEBUG;
        else if (level_str == "INFO") min_level_ = Level::INFO;
        else if (level_str == "WARN") min_level_ = Level::WARN;
        else if (level_str == "ERROR") min_level_ = Level::ERROR;
        else if (level_str == "NONE") min_level_ = Level::NONE;
    }
}

void Logger::setLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

Logger::Level Logger::getLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return min_level_;
}

void Logger::setShowTimestamp(bool show) {
    std::lock_guard<std::mutex> lock(mutex_);
    show_timestamp_ = show;
}

void Logger::setShowComponent(bool show) {
    std::lock_guard<std::mutex> lock(mutex_);
    show_component_ = show;
}

const char* Logger::levelToString(Level level) const {
    switch (level) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO ";
        case Level::WARN:  return "WARN ";
        case Level::ERROR: return "ERROR";
        default:           return "?????";
    }
}

std::string Logger::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm;
    #ifdef _WIN32
        localtime_s(&tm, &time_t_now);
    #else
        localtime_r(&time_t_now, &tm);
    #endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void Logger::log(Level level, const std::string& component, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (level < min_level_) {
        return;
    }
    
    std::ostream& out = (level >= Level::WARN) ? std::cerr : std::cout;
    
    if (show_timestamp_) {
        out << "[" << getTimestamp() << "] ";
    }
    
    out << "[" << levelToString(level) << "] ";
    
    if (show_component_ && !component.empty()) {
        out << "[" << component << "] ";
    }
    
    out << message << std::endl;
}

} // namespace rpc
} // namespace Nexus
