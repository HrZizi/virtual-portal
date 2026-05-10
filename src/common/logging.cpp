#include "logging.h"
#include <iostream>
#include <chrono>
#include <iomanip>

std::ofstream Logger::s_file;
std::mutex Logger::s_mutex;
bool Logger::s_debugEnabled = false;

void Logger::Initialize(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_file.open(filePath, std::ios::out | std::ios::app);
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level == LogLevel::DEBUG && !s_debugEnabled) return;

    std::lock_guard<std::mutex> lock(s_mutex);
    
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::WARN: levelStr = "WARN"; break;
        case LogLevel::ERR:  levelStr = "ERR "; break;
        case LogLevel::DEBUG: levelStr = "DBUG"; break;
    }

    if (s_file.is_open()) {
        s_file << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
               << " [" << levelStr << "] " << message << '\n';
        s_file.flush(); // Flush explicitly to avoid log loss on crash

    }
}
