#pragma once
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// NFC Generator — Creates valid 1024-byte MIFARE Classic 1K dumps for
// Skylanders: Spyro's Adventure figures.
//
// The portal serves raw tag bytes (the game handles decryption), so no
// AES/MD5 is needed. Only CRC16-CCITT and BCC are required.
// ---------------------------------------------------------------------------

namespace NfcGenerator {

    // CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, no reflect, no XOR-out
    uint16_t CRC16_CCITT(const uint8_t* data, size_t len);

    // BCC: XOR of 4 NUID bytes
    uint8_t ComputeBCC(const uint8_t nuid[4]);

    // Generate a fresh 1024-byte .sky buffer for the given character ID.
    //
    // Layout:
    //   Block 0 [0x00-0x0F]: NUID(4) | BCC(1) | SAK=0x81(1) | ATQA=0x0F01(2) | zeros(8)
    //   Block 1 [0x10-0x1F]: CharID(3 LE) | Error=0(1) | CardID=0(8) | Variant=0(2) | CRC16(2)
    //   Blocks 2-63: zeroed (fresh figure, no gameplay data)
    //
    // Uses deterministic NUID: { charId & 0xFF, (charId >> 8) & 0xFF, 0xAC, 0x71 }
    //
    // Returns true on success. outBuffer must be at least 1024 bytes.
    bool GenerateSkyBuffer(uint16_t charId, uint8_t outBuffer[1024]);

}
