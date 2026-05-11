import hashlib, os
from Crypto.Cipher import AES

ref = open(r"E:\skylanders_addons\virtual_portal\figures\Whirlwind.sky", "rb").read()

HASH_CONST = b" Copyright (C) 2010 Activision. All Rights Reserved. \x00"
header = ref[0:32]

print("Block status:")
for i in range(64):
    b = ref[i*16:(i+1)*16]
    is_trailer = (i % 4 == 3)
    
    if i < 4 or is_trailer:
        print(f"Block {i:2d} - skip (header/trailer)")
        continue
        
    # Check if block is all zeros
    if all(x == 0 for x in b):
        print(f"Block {i:2d} - all zeros in file")
        continue
        
    # Try decrypting
    key_input = header + bytes([i]) + HASH_CONST
    key = hashlib.md5(key_input).digest()
    cipher = AES.new(key, AES.MODE_ECB)
    decrypted = cipher.decrypt(b)
    
    if all(x == 0 for x in decrypted):
        print(f"Block {i:2d} - encrypted zeros")
    else:
        print(f"Block {i:2d} - encrypted DATA: {decrypted.hex()}")
