#pragma once
#include <cstdint>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Portal State — Persists the user's slot selections to portal_state.ini
// so figures are automatically restored on next game launch.
//
// Separate from portal_config.ini (which is the user's read-only config).
// ---------------------------------------------------------------------------

namespace PortalState {

    struct SlotEntry {
        uint8_t  slot;      // slot index (0, 1, 2)
        uint16_t charId;    // character ID, or 0xFFFF for empty
    };

    // Read portal_state.ini and return the saved slot entries.
    // Returns empty vector if file doesn't exist or can't be parsed.
    std::vector<SlotEntry> Load(const std::string& path = "portal_state.ini");

    // Write the given slot entries to portal_state.ini.
    // This is a synchronous write (caller should use a detached thread if async is needed).
    void Save(const std::vector<SlotEntry>& entries, const std::string& path = "portal_state.ini");

    // Convenience: snapshot the current FigureManager state and save it.
    void SaveCurrentState(const std::string& path = "portal_state.ini");

}
