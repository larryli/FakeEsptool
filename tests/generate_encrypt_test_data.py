"""
Generate test data for encrypt module compatibility testing.

This script uses espsecure to encrypt data, which can then be used
in C tests to verify compatibility.
"""

import os
import sys
import struct

def generate_test_data():
    """Generate test data for encrypt module testing."""
    
    # Create test data directory
    os.makedirs("test_data", exist_ok=True)
    
    # Test keys
    # 256-bit key (32 bytes) for XTS-AES-128
    key_xts_256 = bytes(range(1, 33))
    with open("test_data/key_xts_256.bin", "wb") as f:
        f.write(key_xts_256)
    
    # 512-bit key (64 bytes) for XTS-AES-256
    key_xts_512 = bytes(range(1, 65))
    with open("test_data/key_xts_512.bin", "wb") as f:
        f.write(key_xts_512)
    
    # Plaintext data (16 bytes = 1 XTS block)
    plaintext_16 = bytes(range(16))
    with open("test_data/plaintext_16.bin", "wb") as f:
        f.write(plaintext_16)
    
    # Plaintext data (48 bytes = 3 XTS blocks)
    plaintext_48 = bytes(range(48))
    with open("test_data/plaintext_48.bin", "wb") as f:
        f.write(plaintext_48)
    
    # Encrypt using espsecure (AES-XTS mode) with 256-bit key
    print("Generating AES-XTS test data with 256-bit key...")
    os.system(
        "python -m espsecure encrypt-flash-data "
        "-k test_data/key_xts_256.bin "
        "-o test_data/encrypted_xts_256_16.bin "
        "-a 0x10000 "
        "--aes-xts "
        "test_data/plaintext_16.bin"
    )
    
    os.system(
        "python -m espsecure encrypt-flash-data "
        "-k test_data/key_xts_256.bin "
        "-o test_data/encrypted_xts_256_48.bin "
        "-a 0x10000 "
        "--aes-xts "
        "test_data/plaintext_48.bin"
    )
    
    # Encrypt using espsecure (AES-XTS mode) with 512-bit key
    print("Generating AES-XTS test data with 512-bit key...")
    os.system(
        "python -m espsecure encrypt-flash-data "
        "-k test_data/key_xts_512.bin "
        "-o test_data/encrypted_xts_512_16.bin "
        "-a 0x10000 "
        "--aes-xts "
        "test_data/plaintext_16.bin"
    )
    
    # Generate C header file with test data
    print("Generating C header file...")
    
    with open("encrypt_test_data.h", "w") as f:
        f.write("/* Auto-generated test data for encrypt module */\n\n")
        f.write("#ifndef ENCRYPT_TEST_DATA_H\n")
        f.write("#define ENCRYPT_TEST_DATA_H\n\n")
        
        # Key XTS 256
        f.write("/* 256-bit key for AES-XTS (XTS-AES-128) */\n")
        f.write("static const BYTE key_xts_256[32] = {\n    ")
        for i, b in enumerate(key_xts_256):
            f.write(f"0x{b:02X}")
            if i < len(key_xts_256) - 1:
                f.write(", ")
            if (i + 1) % 16 == 0 and i < len(key_xts_256) - 1:
                f.write("\n    ")
        f.write("\n};\n\n")
        
        # Key XTS 512
        f.write("/* 512-bit key for AES-XTS (XTS-AES-256) */\n")
        f.write("static const BYTE key_xts_512[64] = {\n    ")
        for i, b in enumerate(key_xts_512):
            f.write(f"0x{b:02X}")
            if i < len(key_xts_512) - 1:
                f.write(", ")
            if (i + 1) % 16 == 0 and i < len(key_xts_512) - 1:
                f.write("\n    ")
        f.write("\n};\n\n")
        
        # Plaintext 16
        f.write("/* 16-byte plaintext (1 XTS block) */\n")
        f.write("static const BYTE plaintext_16[16] = {\n    ")
        for i, b in enumerate(plaintext_16):
            f.write(f"0x{b:02X}")
            if i < len(plaintext_16) - 1:
                f.write(", ")
            if (i + 1) % 16 == 0 and i < len(plaintext_16) - 1:
                f.write("\n    ")
        f.write("\n};\n\n")
        
        # Plaintext 48
        f.write("/* 48-byte plaintext (3 XTS blocks) */\n")
        f.write("static const BYTE plaintext_48[48] = {\n    ")
        for i, b in enumerate(plaintext_48):
            f.write(f"0x{b:02X}")
            if i < len(plaintext_48) - 1:
                f.write(", ")
            if (i + 1) % 16 == 0 and i < len(plaintext_48) - 1:
                f.write("\n    ")
        f.write("\n};\n\n")
        
        # Encrypted XTS 256 (16 bytes)
        if os.path.exists("test_data/encrypted_xts_256_16.bin"):
            with open("test_data/encrypted_xts_256_16.bin", "rb") as ef:
                encrypted = ef.read()
            f.write("/* Encrypted data using AES-XTS with 256-bit key (16 bytes) */\n")
            f.write(f"static const BYTE encrypted_xts_256_16[{len(encrypted)}] = {{\n    ")
            for i, b in enumerate(encrypted):
                f.write(f"0x{b:02X}")
                if i < len(encrypted) - 1:
                    f.write(", ")
                if (i + 1) % 16 == 0 and i < len(encrypted) - 1:
                    f.write("\n    ")
            f.write("\n};\n\n")
        
        # Encrypted XTS 256 (48 bytes)
        if os.path.exists("test_data/encrypted_xts_256_48.bin"):
            with open("test_data/encrypted_xts_256_48.bin", "rb") as ef:
                encrypted = ef.read()
            f.write("/* Encrypted data using AES-XTS with 256-bit key (48 bytes) */\n")
            f.write(f"static const BYTE encrypted_xts_256_48[{len(encrypted)}] = {{\n    ")
            for i, b in enumerate(encrypted):
                f.write(f"0x{b:02X}")
                if i < len(encrypted) - 1:
                    f.write(", ")
                if (i + 1) % 16 == 0 and i < len(encrypted) - 1:
                    f.write("\n    ")
            f.write("\n};\n\n")
        
        # Encrypted XTS 512 (16 bytes)
        if os.path.exists("test_data/encrypted_xts_512_16.bin"):
            with open("test_data/encrypted_xts_512_16.bin", "rb") as ef:
                encrypted = ef.read()
            f.write("/* Encrypted data using AES-XTS with 512-bit key (16 bytes) */\n")
            f.write(f"static const BYTE encrypted_xts_512_16[{len(encrypted)}] = {{\n    ")
            for i, b in enumerate(encrypted):
                f.write(f"0x{b:02X}")
                if i < len(encrypted) - 1:
                    f.write(", ")
                if (i + 1) % 16 == 0 and i < len(encrypted) - 1:
                    f.write("\n    ")
            f.write("\n};\n\n")
        
        f.write("#endif /* ENCRYPT_TEST_DATA_H */\n")
    
    print("Done! Generated test_data/encrypt_test_data.h")

if __name__ == "__main__":
    generate_test_data()
