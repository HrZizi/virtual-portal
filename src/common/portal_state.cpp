#include "portal_state.h"
#include "logging.h"
#include <fstream>
#include <sstream>
#include <algorithm>

// ---------------------------------------------------------------------------
// Load portal_state.ini
//
// Format:
//   [slots]
//   slot_0 = 16
//   slot_1 = 14
//   slot_2 = -1
//
// Values are character IDs. -1 means empty.
// ---------------------------------------------------------------------------
std::vector<PortalState::SlotEntry> PortalState::Load(const std::string& path) {
    std::vector<SlotEntry> entries;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_INFO("portal_state.ini not found, starting with empty slots");
        return entries;
    }

    std::string line;
    bool inSlots = false;
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(line.begin(), std::find_if(line.begin(), line.end(),
            [](unsigned char ch) { return !std::isspace(ch); }));
        line.erase(std::find_if(line.rbegin(), line.rend(),
            [](unsigned char ch) { return !std::isspace(ch); }).base(), line.end());

        if (line.empty() || line[0] == ';') continue;

        if (line == "[slots]") {
            inSlots = true;
            continue;
        }
        if (line[0] == '[') {
            inSlots = false;
            continue;
        }

        if (!inSlots) continue;

        // Parse "slot_N = value"
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim both
        key.erase(key.begin(), std::find_if(key.begin(), key.end(),
            [](unsigned char ch) { return !std::isspace(ch); }));
        key.erase(std::find_if(key.rbegin(), key.rend(),
            [](unsigned char ch) { return !std::isspace(ch); }).base(), key.end());
        val.erase(val.begin(), std::find_if(val.begin(), val.end(),
            [](unsigned char ch) { return !std::isspace(ch); }));
        val.erase(std::find_if(val.rbegin(), val.rend(),
            [](unsigned char ch) { return !std::isspace(ch); }).base(), val.end());

        // Extract slot index from "slot_N"
        if (key.substr(0, 5) != "slot_") continue;
        try {
            uint8_t slotIdx = static_cast<uint8_t>(std::stoi(key.substr(5)));
            int charIdVal = std::stoi(val);
            uint16_t charId = (charIdVal < 0) ? 0xFFFF : static_cast<uint16_t>(charIdVal);
            entries.push_back({ slotIdx, charId });
        } catch (...) {
            LOG_WARN("Failed to parse portal_state.ini entry: " + line);
        }
    }

    LOG_INFO("Loaded " + std::to_string(entries.size()) + " slot entries from portal_state.ini");
    return entries;
}

// ---------------------------------------------------------------------------
// Save slot entries to portal_state.ini
// ---------------------------------------------------------------------------
void PortalState::Save(const std::vector<SlotEntry>& entries, const std::string& path) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERR("Failed to write portal_state.ini");
        return;
    }

    file << "[slots]\n";
    for (const auto& entry : entries) {
        int val = (entry.charId == 0xFFFF) ? -1 : static_cast<int>(entry.charId);
        file << "slot_" << static_cast<int>(entry.slot) << " = " << val << "\n";
    }

    LOG_INFO("Saved " + std::to_string(entries.size()) + " slot entries to portal_state.ini");
}

// ---------------------------------------------------------------------------
// SaveCurrentState — Convenience wrapper
// ---------------------------------------------------------------------------
// Forward-declared; will use FigureManager. Implemented here with a late include
// to avoid circular dependency.
#include "figure_manager.h"

void PortalState::SaveCurrentState(const std::string& path) {
    std::vector<SlotEntry> entries;
    // Save slots 0, 1, 2 (the 3 UI-exposed slots)
    for (uint8_t i = 0; i < 3; ++i) {
        const FigureSlot& slot = FigureManager::Get().GetSlot(i);
        uint16_t charId = slot.occupied ? slot.char_id : 0xFFFF;
        entries.push_back({ i, charId });
    }
    Save(entries, path);
}
