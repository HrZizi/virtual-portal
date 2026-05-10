#!/usr/bin/env python3
"""
generate_skylander.py
~~~~~~~~~~~~~~~~~~~~~
Generates a fresh, valid .sky file for any Skylander from Spyro's Adventure.

The .sky format is a raw 1024-byte dump of a MIFARE Classic 1K NFC tag
(64 blocks × 16 bytes).

File layout (block indices are decimal, byte offsets are hex):
  Block 0  (0x000): Manufacturer block — NUID (4 B) | BCC (1 B) | SAK=0x81 (1 B) | ATQA=0x0F 0x01 (2 B) | zeros
  Block 1  (0x010): Header — CharID (3 B LE) | Error (1 B) | TradingCardID (8 B) | Variant (2 B) | CRC16 (2 B)
  Block 2  (0x020): unused/zeros
  Block 3  (0x030): Sector 0 Trailer — Key A (6 B) | Access (4 B) | Key B (6 B)
  Blocks 4–6        Sector 1 data
  Block 7           Sector 1 Trailer
  ...repeated for all 16 sectors...
  Every 4th block starting at 3 is a Sector Trailer.

Data regions (where game-play data lives) are at:
  Primary:   blocks 8–11 and 12–15 (sectors 2–3 | 0x080–0x0FF)
  Secondary: blocks 24–27 and 28–31 (sectors 6–7 | 0x180–0x1FF)

Both regions are mirrored on real hardware; for a fresh figure we write both.

The CRC-16/CCITT-FALSE polynomial (0x1021, init 0xFFFF, no reflect, no XOR-out)
is used for the header checksum at block 1 bytes 14–15, covering bytes 0x00–0x1D.

For SSA, the encryption used by the game is AES-128-ECB per data block,
key = MD5(sector0_first32_bytes + block_index_byte + ACTIVISION_CONSTANT).
However: the PORTAL serves raw tag bytes (the game itself decrypts). So for our
emulator the .sky file must contain ENCRYPTED block data ONLY IF the game
decrypts on reads. The Dolphin emulator and NefariousTechSupport Runes project
confirm that PORTAL→GAME data transfers are raw (unencrypted from portal side).

Therefore this generator writes:
  - Block 0: manufacturer header
  - Block 1: plaintext header (CharID, Variant, CRC16)
  - All data blocks: zeros (fresh figure)
  - Sector trailers: zeros (keys will be irrelevant since we own the dump)

The game will recognise it as a brand-new, never-used Skylander and let you
name and start playing with it.

Usage:
    python generate_skylander.py [--list]
    python generate_skylander.py <character_name_or_id> [--out <filename>]

Examples:
    python generate_skylander.py spyro
    python generate_skylander.py "Gill Grunt" --out gill.sky
    python generate_skylander.py 16 --out spyro.sky
    python generate_skylander.py --list
"""

import argparse
import os
import struct
import sys
import os

# ---------------------------------------------------------------------------
# SSA Character database (ID → (name, element))
# IDs sourced from the Skylander-IDs community database and confirmed against
# the NefariousTechSupport/Runes kTfbSpyroTag_ToyType.hpp reverse-engineering.
# ---------------------------------------------------------------------------
SSA_CHARACTERS = {
    0:  ("Whirlwind",       "Air"),
    1:  ("Sonic Boom",      "Air"),
    2:  ("Warnado",         "Air"),
    3:  ("Lightning Rod",   "Air"),
    4:  ("Bash",            "Earth"),
    5:  ("Terrafin",        "Earth"),
    6:  ("Dino-Rang",       "Earth"),
    7:  ("Prism Break",     "Earth"),
    8:  ("Sunburn",         "Fire"),
    9:  ("Eruptor",         "Fire"),
    10: ("Ignitor",         "Fire"),
    11: ("Flameslinger",    "Fire"),
    12: ("Zap",             "Water"),
    13: ("Wham-Shell",      "Water"),
    14: ("Gill Grunt",      "Water"),
    15: ("Slam Bam",        "Water"),
    16: ("Spyro",           "Magic"),
    17: ("Voodood",         "Magic"),
    18: ("Double Trouble",  "Magic"),
    19: ("Trigger Happy",   "Tech"),
    20: ("Drill Sergeant",  "Tech"),
    21: ("Drobot",          "Tech"),
    22: ("Boomer",          "Tech"),
    23: ("Wrecking Ball",   "Magic"),
    24: ("Stealth Elf",     "Life"),
    25: ("Stump Smash",     "Life"),
    26: ("Zook",            "Life"),
    27: ("Camo",            "Life"),
    28: ("Ghost Roaster",   "Undead"),
    29: ("Hex",             "Undead"),
    30: ("Cynder",          "Undead"),
    31: ("Chop Chop",       "Undead"),
    # Legendary variants (re-use same char IDs with different Variant IDs)
    # Included as aliases for convenience
    # 32+: Giants characters (not SSA — excluded here)
}

# Variant IDs for SSA figures
# VariantID layout (16-bit):
#   [7:0]  = DecoID (0x00 = standard, 0x01 = metallic, etc.)
#   [8]    = SuperCharger flag (0 for SSA)
#   [9]    = LightCore flag (0 for SSA)
#   [10]   = In-game variant flag
#   [11]   = Repose flag (has Wow Pow)
#   [15:12]= Year code (ESkylandersGame): 0 = SSA, 1 = Giants, etc.
VARIANT_SSA_SERIES1 = 0x0000  # Standard SSA Series 1, Deco 0, year 0

# ---------------------------------------------------------------------------
# MIFARE Classic 1K constants
# ---------------------------------------------------------------------------
BLOCK_SIZE  = 16
BLOCK_COUNT = 64
TAG_SIZE    = BLOCK_SIZE * BLOCK_COUNT  # 1024 bytes

# Fixed manufacturer bytes (all Skylanders figures report these)
SAK  = 0x81
ATQA = bytes([0x0F, 0x01])  # little-endian as stored on tag

# Sector trailer positions (every 4th block starting at 3)
TRAILER_BLOCKS = {3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 63}

# ---------------------------------------------------------------------------
# CRC-16/CCITT-FALSE
# Poly=0x1021, Init=0xFFFF, RefIn=False, RefOut=False, XorOut=0x0000
# ---------------------------------------------------------------------------
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

# ---------------------------------------------------------------------------
# BCC: XOR of the 4 NUID bytes
# ---------------------------------------------------------------------------
def compute_bcc(nuid: bytes) -> int:
    bcc = 0
    for b in nuid:
        bcc ^= b
    return bcc

# ---------------------------------------------------------------------------
# Build a fresh .sky dump
# ---------------------------------------------------------------------------
def generate_sky(char_id: int, variant_id: int = VARIANT_SSA_SERIES1,
                 nuid: bytes = None, trading_card_id: int = 0) -> bytes:
    """
    Build a 1024-byte .sky NFC dump for the given character.

    Args:
        char_id:        The integer character ID (0–31 for SSA).
        variant_id:     16-bit variant. Default is Series 1 standard.
        nuid:           4 bytes. If None, a stable deterministic NUID is
                        generated from the char_id so repeated calls produce
                        the same file.
        trading_card_id: 64-bit trading card ID. 0 is fine for emulation.
    Returns:
        bytes of length 1024.
    """
    tag = bytearray(TAG_SIZE)

    # ------------------------------------------------------------------
    # 1. Choose a deterministic NUID if none provided
    # ------------------------------------------------------------------
    if nuid is None:
        # Stable: encode char_id into bytes 0–1, fill rest with fixed markers
        nuid = bytes([char_id & 0xFF, (char_id >> 8) & 0xFF, 0xAC, 0x71])

    assert len(nuid) == 4, "NUID must be exactly 4 bytes"

    # ------------------------------------------------------------------
    # 2. Block 0 — Manufacturer block
    # ------------------------------------------------------------------
    bcc = compute_bcc(nuid)
    block0 = bytearray(16)
    block0[0:4] = nuid
    block0[4]   = bcc
    block0[5]   = SAK
    block0[6:8] = ATQA
    # bytes 8–15: zeroed (manufacturer info, not read by game)
    tag[0:16] = block0

    # ------------------------------------------------------------------
    # 3. Block 1 — Header (plaintext, game reads this directly)
    #
    # Offset | Size | Description
    # 0x00   |  3 B | Character ID (24-bit little-endian)
    # 0x03   |  1 B | Error byte (0x00 = valid)
    # 0x04   |  8 B | Trading Card ID (uint64 LE)
    # 0x0C   |  2 B | Variant ID (uint16 LE)
    # 0x0E   |  2 B | CRC16-CCITT/FALSE of bytes 0x00–0x1D of this block (0–29)
    #
    # The CRC covers the first 0x1E bytes of the HEADER (blocks 0 and 1 together,
    # i.e. bytes at tag offsets 0x00–0x1D), per the NefariousTechSupport Runes docs.
    # ------------------------------------------------------------------
    block1 = bytearray(16)

    # Character ID: 3 bytes LE at offset 0x10 (tag offset), i.e. block1[0:3]
    block1[0] = char_id & 0xFF
    block1[1] = (char_id >> 8) & 0xFF
    block1[2] = (char_id >> 16) & 0xFF

    # Error byte
    block1[3] = 0x00

    # Trading Card ID (8 bytes LE)
    struct.pack_into("<Q", block1, 4, trading_card_id)

    # Variant ID (2 bytes LE)
    struct.pack_into("<H", block1, 12, variant_id)

    # CRC16: covers bytes 0x00–0x1D of the header region = block0 + block1[0:14]
    crc_data = bytes(block0) + bytes(block1[0:14])
    checksum = crc16_ccitt(crc_data)
    struct.pack_into("<H", block1, 14, checksum)

    tag[16:32] = block1

    # ------------------------------------------------------------------
    # 4. Block 2 — zeroed (part of sector 0 data, unused by SSA)
    # Block 3 — Sector 0 Trailer (leave as zeros; keys are irrelevant for emulation)
    # ------------------------------------------------------------------
    # Already zeroed by bytearray init.

    # ------------------------------------------------------------------
    # 5. All other data blocks: zeroed (fresh figure, no game-play data)
    #    Sector trailers: also zeroed
    # ------------------------------------------------------------------
    # All blocks are already zeroed. Done.

    return bytes(tag)


# ---------------------------------------------------------------------------
# Validation helpers
# ---------------------------------------------------------------------------
def verify_sky(data: bytes) -> dict:
    """Parse and verify a .sky file's header. Returns a dict of parsed fields."""
    if len(data) < 32:
        raise ValueError("File too small to be a valid .sky dump")

    block0 = data[0:16]
    block1 = data[16:32]

    nuid       = block0[0:4]
    bcc        = block0[4]
    sak        = block0[5]
    atqa       = block0[6:8]
    char_id    = block1[0] | (block1[1] << 8) | (block1[2] << 16)
    error_byte = block1[3]
    card_id    = struct.unpack_from("<Q", block1, 4)[0]
    variant_id = struct.unpack_from("<H", block1, 12)[0]
    crc_stored = struct.unpack_from("<H", block1, 14)[0]

    crc_data   = bytes(block0) + bytes(block1[0:14])
    crc_calc   = crc16_ccitt(crc_data)
    crc_ok     = (crc_calc == crc_stored)

    bcc_calc   = compute_bcc(nuid)
    bcc_ok     = (bcc_calc == bcc)

    char_name  = SSA_CHARACTERS.get(char_id, (f"Unknown (ID={char_id})", "?"))[0]
    element    = SSA_CHARACTERS.get(char_id, ("?", "Unknown"))[1]
    variant_year = (variant_id >> 12) & 0xF
    is_lightcore = bool((variant_id >> 9) & 1)
    deco_id    = variant_id & 0xFF

    return {
        "character":    char_name,
        "element":      element,
        "char_id":      char_id,
        "variant_id":   variant_id,
        "variant_year": variant_year,
        "is_lightcore": is_lightcore,
        "deco_id":      deco_id,
        "nuid":         nuid.hex().upper(),
        "bcc_ok":       bcc_ok,
        "sak":          hex(sak),
        "atqa":         atqa.hex().upper(),
        "error_byte":   error_byte,
        "card_id":      card_id,
        "crc_stored":   hex(crc_stored),
        "crc_calc":     hex(crc_calc),
        "crc_ok":       crc_ok,
        "file_size":    len(data),
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def list_characters():
    print("\nSSA Skylanders available for generation:")
    print(f"  {'ID':>3}  {'Name':<20}  {'Element'}")
    print("  " + "-" * 40)
    for cid, (name, element) in sorted(SSA_CHARACTERS.items()):
        print(f"  {cid:>3}  {name:<20}  {element}")
    print()


def resolve_character(query: str) -> tuple:
    """Resolve a name or ID string to (char_id, name, element)."""
    # Try numeric ID first
    try:
        cid = int(query)
        if cid in SSA_CHARACTERS:
            name, element = SSA_CHARACTERS[cid]
            return cid, name, element
        else:
            print(f"[ERROR] Character ID {cid} not in SSA roster.")
            sys.exit(1)
    except ValueError:
        pass

    # Case-insensitive name search
    q = query.strip().lower()
    for cid, (name, element) in SSA_CHARACTERS.items():
        if name.lower() == q:
            return cid, name, element

    # Partial match
    matches = [(cid, name, element) for cid, (name, element) in SSA_CHARACTERS.items()
               if q in name.lower()]
    if len(matches) == 1:
        return matches[0]
    elif len(matches) > 1:
        print(f"[ERROR] Ambiguous name '{query}'. Matches:")
        for cid, name, elem in matches:
            print(f"  {cid:>3}  {name}  ({elem})")
        sys.exit(1)
    else:
        print(f"[ERROR] No character found matching '{query}'.")
        print("Use --list to see available characters.")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Generate a fresh .sky figure dump for Skylanders: Spyro's Adventure",
        epilog="Examples:\n"
               "  python generate_skylander.py spyro\n"
               "  python generate_skylander.py 'Gill Grunt' --out gill.sky\n"
               "  python generate_skylander.py 16 --out spyro.sky\n"
               "  python generate_skylander.py --list\n"
               "  python generate_skylander.py --verify existing.sky",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("character", nargs="?",
                        help="Character name or ID number")
    parser.add_argument("--out", "-o",
                        help="Output filename (default: <name>.sky)")
    parser.add_argument("--list", "-l", action="store_true",
                        help="List all available SSA characters and exit")
    parser.add_argument("--verify", "-v",
                        help="Verify and display info for an existing .sky file")
    parser.add_argument("--variant", type=lambda x: int(x, 0), default=0,
                        help="Variant ID (hex or decimal, default: 0x0000 = Series 1)")
    parser.add_argument("--nuid",
                        help="Custom NUID as 8 hex chars e.g. DEADBEEF (default: auto)")

    args = parser.parse_args()

    if args.list:
        list_characters()
        return

    if args.verify:
        with open(args.verify, "rb") as f:
            data = f.read()
        info = verify_sky(data)
        sz_ok  = "[OK]"  if info["file_size"] == 1024 else "[FAIL] expected 1024"
        bcc_v  = "[OK]"  if info["bcc_ok"]            else "[FAIL]"
        err_v  = "[OK]"  if info["error_byte"] == 0   else "[FAIL] non-zero = rejected"
        crc_v  = "[OK]"  if info["crc_ok"]             else "[FAIL] MISMATCH"
        print(f"\n.sky file analysis: {args.verify}")
        print(f"  File size:    {info['file_size']} bytes  {sz_ok}")
        print(f"  Character:    {info['character']} (ID={info['char_id']}, Element={info['element']})")
        print(f"  Variant ID:   0x{info['variant_id']:04X}  (year={info['variant_year']}, LightCore={info['is_lightcore']}, Deco={info['deco_id']})")
        print(f"  NUID:         {info['nuid']}  BCC {bcc_v}")
        print(f"  SAK:          {info['sak']}  ATQA: {info['atqa']}")
        print(f"  Error byte:   {info['error_byte']}  {err_v}")
        print(f"  Trading Card: {info['card_id']}")
        print(f"  CRC-16:       stored={info['crc_stored']}  calc={info['crc_calc']}  {crc_v}")
        print()
        return

    if not args.character:
        parser.print_help()
        return

    # Resolve character
    char_id, name, element = resolve_character(args.character)

    # Parse custom NUID
    custom_nuid = None
    if args.nuid:
        raw = args.nuid.replace(" ", "")
        if len(raw) != 8:
            print("[ERROR] NUID must be exactly 4 bytes (8 hex chars).")
            sys.exit(1)
        custom_nuid = bytes.fromhex(raw)

    # Generate
    sky_data = generate_sky(char_id, variant_id=args.variant, nuid=custom_nuid)

    # Output path
    out_path = args.out or f"{name.replace(' ', '_').replace('-', '_')}.sky"

    with open(out_path, "wb") as f:
        f.write(sky_data)

    # Verify what we wrote
    info = verify_sky(sky_data)
    crc_sym = "[OK]" if info["crc_ok"] else "[FAIL]"
    bcc_sym = "[OK]" if info["bcc_ok"] else "[FAIL]"

    print(f"\nGenerated {out_path}")
    print(f"  Character: {name} (ID={char_id}, {element})")
    print(f"  Variant:   0x{args.variant:04X}")
    print(f"  NUID:      {info['nuid']}  BCC {bcc_sym}")
    print(f"  CRC-16:    {info['crc_stored']}  {crc_sym}")
    print(f"  Size:      {len(sky_data)} bytes")
    print()
    print(f"Place '{out_path}' in your figures/ folder and update portal_config.ini:")
    print(f"  slot_0 = figures\\{os.path.basename(out_path)}")
    print()


if __name__ == "__main__":
    main()
