#pragma once
#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    INFO,
    WARN,
    ERR,
    DEBUG
};

class Logger {
public:
    static void Initialize(const std::string& filePath);
    static void Log(LogLevel level, const std::string& message);
    static void SetDebugEnabled(bool enabled) { s_debugEnabled = enabled; }

private:
    static std::ofstream s_file;
    static std::mutex s_mutex;
    static bool s_debugEnabled;
};

#define LOG_INFO(msg) Logger::Log(LogLevel::INFO, msg)
#define LOG_WARN(msg) Logger::Log(LogLevel::WARN, msg)
#define LOG_ERR(msg) Logger::Log(LogLevel::ERR, msg)
#define LOG_DEBUG(msg) Logger::Log(LogLevel::DEBUG, msg)
