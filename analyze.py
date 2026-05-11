import hashlib, os
from Crypto.Cipher import AES

# Load the reference Whirlwind figure
ref = open(r"E:\skylanders_addons\virtual_portal\figures\Whirlwind.sky", "rb").read()

# Load our generated Whirlwind (if it exists)
gen_path = os.path.join("D:", os.sep, "Skylanders Spyro's Adventure(TM)", "Mods", "Figures", "Air", "Whirlwind.sky")
gen = open(gen_path, "rb").read() if os.path.exists(gen_path) else None

HASH_CONST = b" Copyright (C) 2010 Activision. All Rights Reserved. \x00"

print("=" * 70)
print("REFERENCE Whirlwind.sky Analysis")
print("=" * 70)
print(f"File size: {len(ref)}")
print(f"Block 0 (header):  {ref[0:16].hex()}")
print(f"Block 1 (header):  {ref[16:32].hex()}")

# Parse header
char_id = ref[0x10] | (ref[0x11] << 8) | (ref[0x12] << 16)
print(f"Character ID: {char_id} (Whirlwind = 0)")
print()

# All non-zero blocks
print("Non-zero blocks in reference:")
for i in range(64):
    b = ref[i*16:(i+1)*16]
    if any(x != 0 for x in b):
        is_trailer = (i % 4 == 3)
        kind = "TRAILER" if is_trailer else "DATA"
        print(f"  Block {i:2d} [{kind:7s}]: {b.hex()}")
print()

# Sector 0 trailer vs others
print(f"Block 3  (sector 0 trailer): {ref[3*16:4*16].hex()}")
print(f"Block 7  (sector 1 trailer): {ref[7*16:8*16].hex()}")
print(f"Block 11 (sector 2 trailer): {ref[11*16:12*16].hex()}")
print()

# Decrypt data blocks to verify key derivation
header = ref[0:32]
print("Decryption test (using known key derivation):")
for blk_idx in [8, 9, 10, 12, 13, 14, 16, 36, 37, 38, 40, 41, 42, 44]:
    raw = ref[blk_idx*16:(blk_idx+1)*16]
    if not any(x != 0 for x in raw):
        continue
    key_input = header + bytes([blk_idx]) + HASH_CONST
    key = hashlib.md5(key_input).digest()
    cipher = AES.new(key, AES.MODE_ECB)
    decrypted = cipher.decrypt(raw)
    print(f"  Block {blk_idx:2d}: decrypted={decrypted.hex()}")
print()

# Now show what SHOULD be in a fresh figure:
# Encrypt 16 zeros for each data block
print("What encrypted zeros look like for a FRESH Whirlwind (same header):")
for blk_idx in [8, 9, 10, 12, 13, 14]:
    key_input = header + bytes([blk_idx]) + HASH_CONST
    key = hashlib.md5(key_input).digest()
    cipher = AES.new(key, AES.MODE_ECB)
    enc_zeros = cipher.encrypt(b"\x00" * 16)
    print(f"  Block {blk_idx:2d}: {enc_zeros.hex()}")

if gen:
    print()
    print("=" * 70)
    print("OUR GENERATED Whirlwind.sky")
    print("=" * 70)
    print(f"Block 0: {gen[0:16].hex()}")
    print(f"Block 1: {gen[16:32].hex()}")
    print(f"Headers match: {ref[0:32] == gen[0:32]}")
    # Show block 8 comparison
    print(f"Block 8 ref: {ref[8*16:9*16].hex()}")
    print(f"Block 8 gen: {gen[8*16:9*16].hex()}")
