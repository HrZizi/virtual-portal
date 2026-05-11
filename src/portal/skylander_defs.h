#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

// ---------------------------------------------------------------------------
// Skylander character definitions for Spyro's Adventure (SSA)
// IDs match the NefariousTechSupport/Runes kTfbSpyroTag_ToyType reverse-engineering
// and the generate_skylander.py database.
// ---------------------------------------------------------------------------

struct SkylanderDef {
    uint16_t    id;
    const char* name;
    const char* element;
};

// Full SSA roster (IDs 0-31)
static const SkylanderDef SSA_CHARACTERS[] = {
    {  0, "Whirlwind",      "Air"    },
    {  1, "Sonic Boom",     "Air"    },
    {  2, "Warnado",        "Air"    },
    {  3, "Lightning Rod",  "Air"    },
    {  4, "Bash",           "Earth"  },
    {  5, "Terrafin",       "Earth"  },
    {  6, "Dino-Rang",      "Earth"  },
    {  7, "Prism Break",    "Earth"  },
    {  8, "Sunburn",        "Fire"   },
    {  9, "Eruptor",        "Fire"   },
    { 10, "Ignitor",        "Fire"   },
    { 11, "Flameslinger",   "Fire"   },
    { 12, "Zap",            "Water"  },
    { 13, "Wham-Shell",     "Water"  },
    { 14, "Gill Grunt",     "Water"  },
    { 15, "Slam Bam",       "Water"  },
    { 16, "Spyro",          "Magic"  },
    { 17, "Voodood",        "Magic"  },
    { 18, "Double Trouble", "Magic"  },
    { 19, "Trigger Happy",  "Tech"   },
    { 20, "Drill Sergeant", "Tech"   },
    { 21, "Drobot",         "Tech"   },
    { 22, "Boomer",         "Tech"   },
    { 23, "Wrecking Ball",  "Magic"  },
    { 24, "Stealth Elf",    "Life"   },
    { 25, "Stump Smash",    "Life"   },
    { 26, "Zook",           "Life"   },
    { 27, "Camo",           "Life"   },
    { 28, "Ghost Roaster",  "Undead" },
    { 29, "Hex",            "Undead" },
    { 30, "Cynder",         "Undead" },
    { 31, "Chop Chop",      "Undead" },

    // Magic Items (SSA)
    { 200, "Anvil Rain",          "Items" },
    { 201, "Sky-Iron Shield",     "Items" },
    { 202, "Healing Elixir",      "Items" },
    { 203, "Time Twister",        "Items" },
    { 204, "Ghost Pirate Swords", "Items" },
    { 205, "Sparx the Dragonfly", "Items" },
    { 206, "Winged Boots",        "Items" },
    { 207, "Hidden Treasure",     "Items" },

    // Expansion Packs / Locations
    { 400, "Dragon's Peak",       "Items" },
    { 401, "Empire of Ice",       "Items" },
    { 402, "Pirate Seas",         "Items" },
    { 403, "Darklight Crypt",     "Items" },
    { 404, "Volcanic Vault",      "Items" },
};

static constexpr size_t SSA_CHARACTER_COUNT = sizeof(SSA_CHARACTERS) / sizeof(SSA_CHARACTERS[0]);

// Element list for UI categorization
static const char* SSA_ELEMENTS[] = {
    "Air", "Earth", "Fire", "Water", "Magic", "Tech", "Life", "Undead", "Items"
};
static constexpr size_t SSA_ELEMENT_COUNT = sizeof(SSA_ELEMENTS) / sizeof(SSA_ELEMENTS[0]);

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

inline const SkylanderDef* FindSkylanderById(uint16_t id) {
    for (size_t i = 0; i < SSA_CHARACTER_COUNT; ++i) {
        if (SSA_CHARACTERS[i].id == id) return &SSA_CHARACTERS[i];
    }
    return nullptr;
}

inline const SkylanderDef* FindSkylanderByName(const char* name) {
    std::string query(name);
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);
    for (size_t i = 0; i < SSA_CHARACTER_COUNT; ++i) {
        std::string n(SSA_CHARACTERS[i].name);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n == query) return &SSA_CHARACTERS[i];
    }
    return nullptr;
}

inline std::vector<const SkylanderDef*> GetSkylandersByElement(const char* element) {
    std::vector<const SkylanderDef*> result;
    for (size_t i = 0; i < SSA_CHARACTER_COUNT; ++i) {
        if (strcmp(SSA_CHARACTERS[i].element, element) == 0) {
            result.push_back(&SSA_CHARACTERS[i]);
        }
    }
    return result;
}
