#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>

static constexpr size_t MAX_SLOTS = 16;
static constexpr size_t FIGURE_SIZE = 1024;
static constexpr size_t BLOCK_SIZE = 16;
static constexpr size_t BLOCK_COUNT = 64;

struct FigureSlot {
    bool occupied = false;
    bool just_added = false;
    bool just_removed = false;
    bool dirty = false;
    uint16_t char_id = 0;          // which character is loaded (SSA ID)
    std::string char_name;         // display name (e.g. "Spyro")
    std::string element;           // element for UI grouping (e.g. "Magic")
    uint8_t data[FIGURE_SIZE] = {0};
    std::string file_path;
};

class FigureManager {
public:
    static FigureManager& Get();
    
    // Load a character by ID. Uses hybrid logic:
    //   1. Check Mods\Figures\<Element>\<Name>.sky — load if exists
    //   2. If not found, generate fresh buffer and save to that path
    bool LoadCharacter(uint8_t slot, uint16_t charId);
    
    // Load from a raw file path (legacy/backward compat)
    bool LoadFigure(uint8_t slot, const std::string& path);
    
    void RemoveFigure(uint8_t slot);
    const uint8_t* ReadBlock(uint8_t slot, uint8_t block) const;
    
    // Updates RAM buffer only — does NOT flush to disk (anti-thrashing).
    // The background flush thread handles periodic writes.
    void WriteBlock(uint8_t slot, uint8_t block, const uint8_t* data);
    
    // Synchronous flush of a single slot or all slots
    void FlushToDisk(uint8_t slot);
    void FlushAll();
    
    // Background flush thread (30-second interval)
    void StartFlushThread();
    void StopFlushThread();
    
    const FigureSlot& GetSlot(uint8_t slot) const { return m_slots[slot]; }
    FigureSlot& GetSlot(uint8_t slot) { return m_slots[slot]; }
    
    // Mutex accessor for external synchronization (e.g., portal_state save)
    std::mutex& GetSlotMutex() { return m_slotMutex; }

private:
    FigureManager() = default;
    FigureSlot m_slots[MAX_SLOTS];
    
    // Flush thread internals
    std::thread m_flushThread;
    std::atomic<bool> m_flushRunning{false};
    std::mutex m_slotMutex;
    
    void FlushThreadFunc();
};
