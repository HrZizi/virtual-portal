#include "portal_emulator.h"
#include "figure_manager.h"
#include "logging.h"
#include <cstring>
#include <chrono>

PortalEmulator& PortalEmulator::Get() {
    static PortalEmulator instance;
    return instance;
}

void PortalEmulator::QueueResponse(const std::array<uint8_t, 32>& resp) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    // Cap queue depth to 4 to prevent unbounded growth during tight ReadFile polling.
    // The game polls ReadFile far faster than we produce meaningful responses.
    if (m_responseQueue.size() < 4) {
        m_responseQueue.push(resp);
    }
}

void PortalEmulator::ProcessCommand(const uint8_t cmd[32]) {
    char cmdChar = (char)cmd[0];
    LOG_DEBUG("Received Command: " + std::string(1, cmdChar));

    std::array<uint8_t, 32> resp = {0};
    resp[0] = cmd[0]; // Most responses mirror the command char

    switch (cmdChar) {
        case 'R': { // Ready
            m_readyCalled = true;
            resp[1] = 0x02; // Portal type (SSA)
            resp[2] = 0x19; 
            QueueResponse(resp);
            LOG_INFO("Portal Ready Called");
            break;
        }
        case 'A': { // Activate
            m_activated = (cmd[1] == 0x01);
            resp[1] = cmd[1];
            resp[2] = 0xFF;
            resp[3] = 0x77;
            QueueResponse(resp);
            LOG_INFO("Portal " + std::string(m_activated ? "Activated" : "Deactivated"));
            break;
        }
        case 'S': { // Status
            GenerateStatusResponse();
            break;
        }
        case 'Q': { // Query
            // Game can send indices with upper nibble set (e.g. 0x20 for slot 0)
            uint8_t slotIdx = cmd[1] % 0x10;
            uint8_t blockIdx = cmd[2];
            
            const uint8_t* blockData = FigureManager::Get().ReadBlock(slotIdx, blockIdx);
            if (blockData) {
                resp[1] = 0x10 + slotIdx;
                resp[2] = blockIdx;
                std::memcpy(&resp[3], blockData, 16);
            } else {
                resp[1] = 0x01; // Error
                resp[2] = blockIdx;
            }
            QueueResponse(resp);
            LOG_DEBUG("Query Slot " + std::to_string(slotIdx) + " (raw cmd[1]=" + std::to_string(cmd[1]) + ") Block " + std::to_string(blockIdx));
            break;
        }
        case 'W': { // Write
            // Game can send indices with upper nibble set
            uint8_t slotIdx = cmd[1] % 0x10;
            uint8_t blockIdx = cmd[2];
            
            FigureManager::Get().WriteBlock(slotIdx, blockIdx, &cmd[3]);
            
            resp[1] = 0x00;
            resp[2] = blockIdx;
            QueueResponse(resp);
            LOG_INFO("Write Slot " + std::to_string(slotIdx) + " (raw cmd[1]=" + std::to_string(cmd[1]) + ") Block " + std::to_string(blockIdx));
            break;
        }
        case 'C': { // Color
            LOG_DEBUG("LED Color set to R:" + std::to_string(cmd[1]) + " G:" + std::to_string(cmd[2]) + " B:" + std::to_string(cmd[3]));
            // No response for 'C'
            break;
        }
        default:
            LOG_WARN("Unknown command received: " + std::to_string((int)cmd[0]));
            break;
    }
}

void PortalEmulator::GenerateStatusResponse() {
    std::array<uint8_t, 32> resp = {0};
    resp[0] = 'S';
    
    uint32_t figureStatus = 0;
    for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
        FigureSlot& slot = FigureManager::Get().GetSlot(i);
        uint8_t val = 0;
        if (slot.occupied) {
            if (slot.just_added) {
                val = 0b11; // Added
                slot.just_added = false;
            } else {
                val = 0b01; // Present
            }
        } else if (slot.just_removed) {
            val = 0b10; // Removed
            slot.just_removed = false;
        }
        
        figureStatus |= (uint32_t(val) << (i * 2));
    }
    
    std::memcpy(&resp[1], &figureStatus, 4);
    resp[5] = m_statusCounter++;
    resp[6] = m_readyCalled ? 0x01 : 0x00;
    
    QueueResponse(resp);
}

bool PortalEmulator::GetNextResponse(uint8_t out[32]) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_responseQueue.empty()) {
        if (m_activated) {
            // Auto-generate status if activated and nothing else is queued
            // In a real device this might be handled by a timer, 
            // but for a hook we can generate it on ReadFile if needed.
            // Let's release the lock before calling GenerateStatusResponse to avoid deadlock
            // (though m_queueMutex is re-entrant if we use recursive_mutex, but we don't)
        }
        return false;
    }
    
    std::array<uint8_t, 32> resp = m_responseQueue.front();
    m_responseQueue.pop();
    std::memcpy(out, resp.data(), 32);
    return true;
}

void PortalEmulator::UpdateStatus() {
    if (!m_activated) return;

    // Rate-limit to ~50 Hz. The game calls ReadFile in a tight loop;
    // without this guard every miss triggers GenerateStatusResponse(),
    // which was the root cause of the 1 GB log explosion.
    using namespace std::chrono;
    auto now = steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(now - m_lastStatusTime).count();
    if (elapsed < 20) return; // 20 ms = 50 Hz
    m_lastStatusTime = now;

    GenerateStatusResponse();
}
