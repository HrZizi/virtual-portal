#pragma once
#include <string>
#include <vector>
#include <cstdint>

static constexpr size_t MAX_SLOTS = 16;
static constexpr size_t FIGURE_SIZE = 1024;
static constexpr size_t BLOCK_SIZE = 16;
static constexpr size_t BLOCK_COUNT = 64;

struct FigureSlot {
    bool occupied = false;
    bool just_added = false;
    bool just_removed = false;
    bool dirty = false;
    uint8_t data[FIGURE_SIZE] = {0};
    std::string file_path;
};

class FigureManager {
public:
    static FigureManager& Get();
    
    bool LoadFigure(uint8_t slot, const std::string& path);
    void RemoveFigure(uint8_t slot);
    const uint8_t* ReadBlock(uint8_t slot, uint8_t block) const;
    void WriteBlock(uint8_t slot, uint8_t block, const uint8_t* data);
    void FlushToDisk(uint8_t slot);
    void FlushAll();
    
    const FigureSlot& GetSlot(uint8_t slot) const { return m_slots[slot]; }
    FigureSlot& GetSlot(uint8_t slot) { return m_slots[slot]; }

private:
    FigureManager() = default;
    FigureSlot m_slots[MAX_SLOTS];
};
