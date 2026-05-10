#include "figure_manager.h"
#include "logging.h"
#include <fstream>
#include <cstring>

FigureManager& FigureManager::Get() {
    static FigureManager instance;
    return instance;
}

bool FigureManager::LoadFigure(uint8_t slot, const std::string& path) {
    if (slot >= MAX_SLOTS) return false;
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERR("Failed to open .sky file: " + path);
        return false;
    }
    
    file.read(reinterpret_cast<char*>(m_slots[slot].data), FIGURE_SIZE);
    if (file.gcount() < (std::streamsize)FIGURE_SIZE) {
        LOG_WARN("File too small, but loaded what was there: " + path);
    }
    
    m_slots[slot].file_path = path;
    m_slots[slot].occupied = true;
    m_slots[slot].just_added = true;
    m_slots[slot].dirty = false;
    
    LOG_INFO("Loaded figure into slot " + std::to_string(slot) + ": " + path);
    return true;
}

void FigureManager::RemoveFigure(uint8_t slot) {
    if (slot >= MAX_SLOTS || !m_slots[slot].occupied) return;
    
    FlushToDisk(slot);
    m_slots[slot].occupied = false;
    m_slots[slot].just_removed = true;
    LOG_INFO("Removed figure from slot " + std::to_string(slot));
}

const uint8_t* FigureManager::ReadBlock(uint8_t slot, uint8_t block) const {
    if (slot >= MAX_SLOTS || block >= BLOCK_COUNT || !m_slots[slot].occupied) return nullptr;
    return &m_slots[slot].data[block * BLOCK_SIZE];
}

void FigureManager::WriteBlock(uint8_t slot, uint8_t block, const uint8_t* data) {
    if (slot >= MAX_SLOTS || block >= BLOCK_COUNT || !m_slots[slot].occupied) return;
    
    std::memcpy(&m_slots[slot].data[block * BLOCK_SIZE], data, BLOCK_SIZE);
    m_slots[slot].dirty = true;
    LOG_DEBUG("Wrote block " + std::to_string(block) + " to slot " + std::to_string(slot));
    
    // For MVP, flush immediately as requested in plan
    FlushToDisk(slot);
}

void FigureManager::FlushToDisk(uint8_t slot) {
    if (slot >= MAX_SLOTS || !m_slots[slot].occupied || !m_slots[slot].dirty) return;
    
    std::ofstream file(m_slots[slot].file_path, std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(m_slots[slot].data), FIGURE_SIZE);
        m_slots[slot].dirty = false;
        LOG_INFO("Flushed figure in slot " + std::to_string(slot) + " to disk");
    } else {
        LOG_ERR("Failed to flush slot " + std::to_string(slot) + " to " + m_slots[slot].file_path);
    }
}

void FigureManager::FlushAll() {
    for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
        FlushToDisk(i);
    }
}
