#include "figure_manager.h"
#include "nfc_generator.h"
#include "skylander_defs.h"
#include "logging.h"
#include <fstream>
#include <cstring>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

FigureManager& FigureManager::Get() {
    static FigureManager instance;
    return instance;
}

// ---------------------------------------------------------------------------
// LoadCharacter — Hybrid file/generate loading
//
// 1. Look up charId in the SSA database
// 2. Build path: Mods\Figures\<Element>\<Name>.sky
// 3. If file exists: load it
// 4. If not: generate fresh buffer and save it
// ---------------------------------------------------------------------------
bool FigureManager::LoadCharacter(uint8_t slot, uint16_t charId) {
    if (slot >= MAX_SLOTS) return false;

    const SkylanderDef* def = FindSkylanderById(charId);
    if (!def) {
        LOG_ERR("Unknown character ID: " + std::to_string(charId));
        return false;
    }

    // Build filename: replace spaces and hyphens with underscores
    std::string safeName(def->name);
    std::replace(safeName.begin(), safeName.end(), ' ', '_');
    std::replace(safeName.begin(), safeName.end(), '-', '_');

    // Path: Mods\Figures\<Element>\<Name>.sky
    fs::path figDir = fs::path("Mods") / "Figures" / def->element;
    fs::path figPath = figDir / (safeName + ".sky");
    std::string pathStr = figPath.string();

    std::lock_guard<std::mutex> lock(m_slotMutex);

    // If slot was occupied, flush it first
    if (m_slots[slot].occupied && m_slots[slot].dirty) {
        FlushToDisk(slot);
    }

    if (fs::exists(figPath)) {
        // Load existing file
        std::ifstream file(figPath, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERR("Failed to open existing .sky file: " + pathStr);
            return false;
        }
        file.read(reinterpret_cast<char*>(m_slots[slot].data), FIGURE_SIZE);
        if (file.gcount() < static_cast<std::streamsize>(FIGURE_SIZE)) {
            LOG_WARN("File smaller than 1024 bytes, loaded what was there: " + pathStr);
        }
        LOG_INFO("Loaded existing figure: " + std::string(def->name) + " from " + pathStr);
    } else {
        // Generate fresh buffer
        if (!NfcGenerator::GenerateSkyBuffer(charId, m_slots[slot].data)) {
            LOG_ERR("Failed to generate NFC buffer for charId " + std::to_string(charId));
            return false;
        }

        // Create directories and save
        std::error_code ec;
        fs::create_directories(figDir, ec);
        if (ec) {
            LOG_ERR("Failed to create directory: " + figDir.string() + " (" + ec.message() + ")");
            return false;
        }

        std::ofstream file(figPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            LOG_ERR("Failed to write new .sky file: " + pathStr);
            return false;
        }
        file.write(reinterpret_cast<const char*>(m_slots[slot].data), FIGURE_SIZE);
        LOG_INFO("Generated new figure: " + std::string(def->name) + " at " + pathStr);
    }

    // Update slot metadata
    m_slots[slot].file_path = pathStr;
    m_slots[slot].occupied = true;
    m_slots[slot].just_added = true;
    m_slots[slot].dirty = false;
    m_slots[slot].char_id = charId;
    m_slots[slot].char_name = def->name;
    m_slots[slot].element = def->element;

    return true;
}

// ---------------------------------------------------------------------------
// LoadFigure — Legacy raw path loading (backward compat)
// ---------------------------------------------------------------------------
bool FigureManager::LoadFigure(uint8_t slot, const std::string& path) {
    if (slot >= MAX_SLOTS) return false;
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERR("Failed to open .sky file: " + path);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_slotMutex);
    
    file.read(reinterpret_cast<char*>(m_slots[slot].data), FIGURE_SIZE);
    if (file.gcount() < (std::streamsize)FIGURE_SIZE) {
        LOG_WARN("File too small, but loaded what was there: " + path);
    }
    
    m_slots[slot].file_path = path;
    m_slots[slot].occupied = true;
    m_slots[slot].just_added = true;
    m_slots[slot].dirty = false;
    
    // Try to parse character ID from block 1 for metadata
    uint16_t parsedId = m_slots[slot].data[0x10] | (m_slots[slot].data[0x11] << 8);
    const SkylanderDef* def = FindSkylanderById(parsedId);
    if (def) {
        m_slots[slot].char_id = parsedId;
        m_slots[slot].char_name = def->name;
        m_slots[slot].element = def->element;
    } else {
        m_slots[slot].char_id = parsedId;
        m_slots[slot].char_name = "Unknown (ID " + std::to_string(parsedId) + ")";
        m_slots[slot].element = "Unknown";
    }
    
    LOG_INFO("Loaded figure into slot " + std::to_string(slot) + ": " + path +
             " (" + m_slots[slot].char_name + ")");
    return true;
}

// ---------------------------------------------------------------------------
// RemoveFigure
// ---------------------------------------------------------------------------
void FigureManager::RemoveFigure(uint8_t slot) {
    if (slot >= MAX_SLOTS) return;
    
    std::lock_guard<std::mutex> lock(m_slotMutex);
    
    if (!m_slots[slot].occupied) return;
    
    // Flush any pending changes before removing
    if (m_slots[slot].dirty) {
        std::ofstream file(m_slots[slot].file_path, std::ios::binary | std::ios::trunc);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(m_slots[slot].data), FIGURE_SIZE);
            m_slots[slot].dirty = false;
            LOG_INFO("Flushed dirty slot " + std::to_string(slot) + " before removal");
        }
    }
    
    m_slots[slot].occupied = false;
    m_slots[slot].just_removed = true;
    m_slots[slot].char_id = 0;
    m_slots[slot].char_name.clear();
    m_slots[slot].element.clear();
    LOG_INFO("Removed figure from slot " + std::to_string(slot));
}

// ---------------------------------------------------------------------------
// ReadBlock
// ---------------------------------------------------------------------------
const uint8_t* FigureManager::ReadBlock(uint8_t slot, uint8_t block) const {
    if (slot >= MAX_SLOTS || block >= BLOCK_COUNT || !m_slots[slot].occupied) return nullptr;
    return &m_slots[slot].data[block * BLOCK_SIZE];
}

// ---------------------------------------------------------------------------
// WriteBlock — Updates RAM buffer only. No immediate disk flush.
// The background flush thread handles periodic writes (anti-thrashing).
// ---------------------------------------------------------------------------
void FigureManager::WriteBlock(uint8_t slot, uint8_t block, const uint8_t* data) {
    if (slot >= MAX_SLOTS || block >= BLOCK_COUNT || !m_slots[slot].occupied) return;
    
    std::lock_guard<std::mutex> lock(m_slotMutex);
    
    std::memcpy(&m_slots[slot].data[block * BLOCK_SIZE], data, BLOCK_SIZE);
    m_slots[slot].dirty = true;
    LOG_DEBUG("Wrote block " + std::to_string(block) + " to slot " + std::to_string(slot) + " (RAM only)");
}

// ---------------------------------------------------------------------------
// FlushToDisk — Write a single slot's buffer to its .sky file
// Caller must hold m_slotMutex OR call from a context where it's safe.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// FlushAll — Synchronous flush of all occupied dirty slots
// ---------------------------------------------------------------------------
void FigureManager::FlushAll() {
    std::lock_guard<std::mutex> lock(m_slotMutex);
    for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
        FlushToDisk(i);
    }
}

// ---------------------------------------------------------------------------
// Background Flush Thread — Sleeps 30 seconds, then flushes all dirty slots.
// Uses 1-second sleep increments for responsive shutdown.
// ---------------------------------------------------------------------------
void FigureManager::FlushThreadFunc() {
    LOG_INFO("Flush thread started (30-second interval)");
    while (m_flushRunning.load()) {
        // Sleep in 1-second chunks for responsive shutdown
        for (int i = 0; i < 30 && m_flushRunning.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!m_flushRunning.load()) break;
        
        std::lock_guard<std::mutex> lock(m_slotMutex);
        int flushed = 0;
        for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
            if (m_slots[i].occupied && m_slots[i].dirty) {
                FlushToDisk(i);
                ++flushed;
            }
        }
        if (flushed > 0) {
            LOG_INFO("Periodic flush: wrote " + std::to_string(flushed) + " dirty slot(s)");
        }
    }
    LOG_INFO("Flush thread stopped");
}

void FigureManager::StartFlushThread() {
    if (m_flushRunning.load()) return;
    m_flushRunning.store(true);
    m_flushThread = std::thread(&FigureManager::FlushThreadFunc, this);
}

void FigureManager::StopFlushThread() {
    if (!m_flushRunning.load()) return;
    m_flushRunning.store(false);
    if (m_flushThread.joinable()) {
        m_flushThread.join();
    }
}
