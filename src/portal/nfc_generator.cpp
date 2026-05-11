#include "nfc_generator.h"
#include <cstring>

// ---------------------------------------------------------------------------
// CRC-16/CCITT-FALSE
// Polynomial: 0x1021, Initial: 0xFFFF, RefIn: false, RefOut: false, XorOut: 0
//
// Mirrors the Python implementation in generate_skylander.py exactly.
// ---------------------------------------------------------------------------
uint16_t NfcGenerator::CRC16_CCITT(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
            crc &= 0xFFFF;
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// BCC: XOR of 4 NUID bytes
// ---------------------------------------------------------------------------
uint8_t NfcGenerator::ComputeBCC(const uint8_t nuid[4]) {
    return nuid[0] ^ nuid[1] ^ nuid[2] ^ nuid[3];
}

// ---------------------------------------------------------------------------
// Generate a fresh 1024-byte .sky buffer
//
// Direct C++ port of generate_sky() from generate_skylander.py, corrected
// against the known-good spyro.sky dump:
//   - Block 0: NUID only (no BCC/SAK/ATQA — game doesn't check them)
//   - Sector trailers: access bits 7F 0F 08 69 (Skylanders standard)
// ---------------------------------------------------------------------------

// Standard Skylanders MIFARE sector trailer: KeyA(6) + AccessBits(4) + KeyB(6)
static const uint8_t SKYLANDER_TRAILER[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Key A (zeros — irrelevant for emulation)
    0x7F, 0x0F, 0x08, 0x69,              // Access bits (standard)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // Key B (zeros — irrelevant for emulation)
};

// Sector 0 has a unique trailer in SSA
static const uint8_t SKYLANDER_TRAILER_S0[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Key A
    0x0F, 0x0F, 0x0F, 0x69,              // Access bits (special for Sector 0)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // Key B
};

bool NfcGenerator::GenerateSkyBuffer(uint16_t charId, uint8_t outBuffer[1024]) {
    // Zero the entire buffer
    std::memset(outBuffer, 0, 1024);

    // 1. Deterministic NUID from character ID
    // Matching reference figures: NUID[0] = charId + 1
    uint8_t nuid[4] = {
        static_cast<uint8_t>((charId + 1) & 0xFF),
        0x00,
        0x00,
        0x00
    };

    // 2. Block 0 — NUID only (simplified format matching known-good figures)
    outBuffer[0] = nuid[0];
    outBuffer[1] = nuid[1];
    outBuffer[2] = nuid[2];
    outBuffer[3] = nuid[3];

    // 3. Block 1 — Header
    outBuffer[0x10] = charId & 0xFF;
    outBuffer[0x11] = (charId >> 8) & 0xFF;
    outBuffer[0x12] = 0x00;  // high byte of 24-bit char ID
    outBuffer[0x13] = 0x00;  // error byte = valid

    // 4. CRC-16 covers bytes 0x00 through 0x1D
    uint16_t crc = CRC16_CCITT(outBuffer, 0x1E);
    outBuffer[0x1E] = crc & 0xFF;
    outBuffer[0x1F] = (crc >> 8) & 0xFF;

    // 5. Write sector trailers
    for (int sector = 0; sector < 16; ++sector) {
        int trailerBlock = sector * 4 + 3;
        const uint8_t* trailer = (sector == 0) ? SKYLANDER_TRAILER_S0 : SKYLANDER_TRAILER;
        std::memcpy(&outBuffer[trailerBlock * 16], trailer, 16);
    }

    return true;
}
