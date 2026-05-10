#pragma once
#include <cstdint>
#include <queue>
#include <array>
#include <mutex>
#include <chrono>

class PortalEmulator {
public:
    static PortalEmulator& Get();

    void ProcessCommand(const uint8_t cmd[32]);
    bool GetNextResponse(uint8_t out[32]);
    
    void UpdateStatus(); // Called periodically or on poll

private:
    PortalEmulator() = default;
    
    void QueueResponse(const std::array<uint8_t, 32>& resp);
    void GenerateStatusResponse();

    bool m_activated = false;
    bool m_readyCalled = false;
    uint8_t m_statusCounter = 0;
    std::queue<std::array<uint8_t, 32>> m_responseQueue;
    std::mutex m_queueMutex;
    std::chrono::steady_clock::time_point m_lastStatusTime{}; // for 50Hz rate-limiting
};
