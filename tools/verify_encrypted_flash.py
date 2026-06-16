#!/usr/bin/env python3
"""
verify_encrypted_flash.py - Verify encrypted flash data in FakeEsptool device file

Decrypts flash data using the encryption key from eFuse and compares it against
the original plaintext binary files to verify that encrypted flashing was correct.

Usage:
    python3 verify_encrypted_flash.py <flash_dir> <device_file>

Arguments:
    flash_dir    - Directory containing flash_args and binary files (plaintext)
    device_file  - FakeEsptool .esp device file to verify

Example:
    python3 verify_encrypted_flash.py build/my_project my_device.esp

Flash Args Format:
    First line: --flash-mode dio --flash-freq 60m --flash-size 2MB
    Subsequent lines: <address> <filepath>
    Example:
        0x0 bootloader/bootloader.bin
        0x10000 my_app.bin
        0x8000 partition_table/partition-table.bin

Requirements:
    - cryptography library: pip install cryptography
"""

import hashlib
import struct
import sys
import os

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.backends import default_backend
except ImportError:
    print("ERROR: cryptography library not found. Install with: pip install cryptography")
    sys.exit(1)

# Device file constants
DEVICE_MAGIC = 0x45535000  # "ESP\0"
DEVICE_VERSION = 1

# Header size: magic(4) + version(4) + chipType(4) + xtalFreq(1) + reserved(3) + mac(6) + reserved(2) + flashSize(4) + efuseSize(4)
HEADER_SIZE = 32

# Chip type mapping
CHIP_TYPES = {
    0: "ESP8266",
    1: "ESP32",
    2: "ESP32-S2",
    3: "ESP32-S3",
    4: "ESP32-C2",
    5: "ESP32-C3",
    6: "ESP32-C6",
}

# Crystal frequency mapping
XTAL_FREQS = {
    0: "40MHz",
    1: "26MHz",
}

# eFuse encryption key offsets and lengths
# (key_offset, key_len) for each chip type
ENCRYPT_KEY_INFO = {
    1: (0x38, 32),   # ESP32: BLOCK1, 256-bit
    2: (0x9C, 64),   # ESP32-S2: BLOCK_KEY0, 512-bit
    3: (0x9C, 64),   # ESP32-S3: BLOCK_KEY0, 512-bit
    4: (0x60, 32),   # ESP32-C2: BLOCK_KEY0, 256-bit
    5: (0x9C, 32),   # ESP32-C3: BLOCK_KEY0, 256-bit
    6: (0x9C, 32),   # ESP32-C6: BLOCK_KEY0, 256-bit
}


def read_device_header(f):
    """Read and parse device file header."""
    magic = struct.unpack('<I', f.read(4))[0]
    if magic != DEVICE_MAGIC:
        raise ValueError(f"Invalid magic: 0x{magic:08X} (expected 0x{DEVICE_MAGIC:08X})")

    version = struct.unpack('<I', f.read(4))[0]
    if version != DEVICE_VERSION:
        raise ValueError(f"Unsupported version: {version}")

    chip_type = struct.unpack('<I', f.read(4))[0]
    xtal_freq = struct.unpack('B', f.read(1))[0]
    reserved1 = f.read(3)
    mac = f.read(6)
    reserved2 = f.read(2)
    flash_size = struct.unpack('<I', f.read(4))[0]
    efuse_size = struct.unpack('<I', f.read(4))[0]

    return {
        'magic': magic,
        'version': version,
        'chip_type': chip_type,
        'xtal_freq': xtal_freq,
        'mac': mac,
        'flash_size': flash_size,
        'efuse_size': efuse_size,
    }


def read_efuse_data(f, efuse_size):
    """Read eFuse data from device file."""
    f.seek(HEADER_SIZE)
    efuse_data = f.read(efuse_size)
    if len(efuse_data) != efuse_size:
        raise ValueError(f"Failed to read eFuse data: expected {efuse_size} bytes, got {len(efuse_data)}")
    return efuse_data


def read_flash_data(f, efuse_size, flash_size):
    """Read flash data from device file."""
    f.seek(HEADER_SIZE + efuse_size)
    flash_data = f.read(flash_size)
    if len(flash_data) != flash_size:
        raise ValueError(f"Failed to read flash data: expected {flash_size} bytes, got {len(flash_data)}")
    return flash_data


def get_encryption_key(efuse_data, chip_type):
    """Extract encryption key from eFuse data."""
    if chip_type not in ENCRYPT_KEY_INFO:
        raise ValueError(f"Chip {CHIP_TYPES.get(chip_type, 'Unknown')} does not support flash encryption")

    key_offset, key_len = ENCRYPT_KEY_INFO[chip_type]

    if key_offset + key_len > len(efuse_data):
        raise ValueError(f"eFuse data too small to extract key: need {key_offset + key_len} bytes, have {len(efuse_data)}")

    key = efuse_data[key_offset:key_offset + key_len]

    # Check if key is all zeros (not programmed)
    if all(b == 0 for b in key):
        raise ValueError("Encryption key not programmed in eFuse (all zeros)")

    return key


def decrypt_flash_data(flash_data, key, flash_addr=0):
    """
    Decrypt flash data using AES-XTS algorithm.

    This implements the same algorithm as espsecure _flash_encryption_operation_aes_xts.

    Args:
        flash_data: Encrypted flash data
        key: AES-XTS key (32 or 64 bytes)
        flash_addr: Starting flash address (must be multiple of 16)

    Returns:
        Decrypted flash data
    """
    backend = default_backend()

    if flash_addr % 16 != 0:
        raise ValueError(f"Flash address 0x{flash_addr:X} must be multiple of 16")

    if len(flash_data) % 16 != 0:
        raise ValueError(f"Flash data length ({len(flash_data)}) must be multiple of 16")

    if len(flash_data) == 0:
        return b""

    indata = flash_data

    # Left pad for 1024-bit (128-byte) aligned address
    pad_left = flash_addr % 0x80
    if pad_left > 0:
        indata = (b"\x00" * pad_left) + indata

    # Right pad for full 1024-bit blocks
    pad_right = len(indata) % 0x80
    if pad_right > 0:
        pad_right = 0x80 - pad_right
        indata = indata + (b"\x00" * pad_right)

    # Split into 128-byte blocks and decrypt each
    output_list = []
    addr = flash_addr & ~0x7F  # Align to 128-byte boundary

    for i in range(0, len(indata), 0x80):
        block = indata[i:i + 0x80]
        tweak = struct.pack("<I", addr) + (b"\x00" * 12)
        addr += 0x80

        cipher = Cipher(algorithms.AES(key), modes.XTS(tweak), backend=backend)
        decryptor = cipher.decryptor()

        # Reverse input, decrypt, reverse output (ESP hardware convention)
        block_reversed = block[::-1]
        decrypted = decryptor.update(block_reversed)
        output_list.append(decrypted[::-1])

    output = b"".join(output_list)

    # Remove padding
    if pad_left > 0:
        output = output[pad_left:]
    if pad_right > 0:
        output = output[:-pad_right]

    return output


def parse_flash_args(flash_dir):
    """Parse flash_args file and return list of (address, filepath) tuples."""
    flash_args_path = os.path.join(flash_dir, 'flash_args')
    if not os.path.exists(flash_args_path):
        raise FileNotFoundError(f"flash_args not found: {flash_args_path}")

    segments = []
    with open(flash_args_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if line.startswith('--'):
                # Skip options line
                continue

            parts = line.split(None, 1)
            if len(parts) == 2:
                addr_str, filepath = parts
                addr = int(addr_str, 0)  # Support hex (0x) and decimal
                segments.append((addr, filepath))

    return segments


def read_bin_file(filepath):
    """Read binary file and return contents."""
    with open(filepath, 'rb') as f:
        return f.read()


def calc_md5(data):
    """Calculate MD5 hash of data."""
    return hashlib.md5(data).hexdigest()


def read_device_file(filepath):
    """Read entire device file and return contents."""
    with open(filepath, 'rb') as f:
        return f.read()


def verify_encrypted_flash(flash_dir, device_file):
    """Verify encrypted flash data in device file."""
    # Parse flash_args
    segments = parse_flash_args(flash_dir)
    if not segments:
        print("ERROR: No flash segments found in flash_args")
        return False

    # Read entire device file for MD5
    device_data = read_device_file(device_file)
    device_file_md5 = calc_md5(device_data)

    # Read device file
    with open(device_file, 'rb') as f:
        header = read_device_header(f)
        efuse_data = read_efuse_data(f, header['efuse_size'])
        flash_data = read_flash_data(f, header['efuse_size'], header['flash_size'])

    # Calculate device flash MD5
    flash_md5 = calc_md5(flash_data)

    # Get encryption key
    try:
        key = get_encryption_key(efuse_data, header['chip_type'])
        key_len = len(key)
        key_md5 = calc_md5(key)
    except ValueError as e:
        print(f"ERROR: {e}")
        return False

    # Pre-load all segment data
    segment_info = []
    for addr, filepath in segments:
        bin_path = os.path.join(flash_dir, filepath)
        if not os.path.exists(bin_path):
            segment_info.append((addr, filepath, None, None, "File not found"))
            continue
        bin_data = read_bin_file(bin_path)
        bin_size = len(bin_data)
        bin_md5 = calc_md5(bin_data)
        if addr + bin_size > header['flash_size']:
            segment_info.append((addr, filepath, bin_data, bin_md5, "Exceeds flash size"))
            continue
        segment_info.append((addr, filepath, bin_data, bin_md5, None))

    # Print flash directory info
    print(f"Flash directory: {flash_dir}")
    for addr, filepath, bin_data, bin_md5, error in segment_info:
        print(f"  - Offset: 0x{addr:08X}")
        print(f"    File: {filepath}")
        if bin_data is not None:
            print(f"    File Size: {len(bin_data)} bytes")
            print(f"    File MD5: {bin_md5}")
        if error:
            print(f"    Error: {error}")
    print()

    # Print device file info
    chip_name = CHIP_TYPES.get(header['chip_type'], f"Unknown")
    xtal_name = XTAL_FREQS.get(header['xtal_freq'], f"Unknown")
    print(f"Device file: {device_file}")
    print(f"  File MD5: {device_file_md5}")
    print(f"  Chip Type: {chip_name} ({header['chip_type']})")
    print(f"  XTAL Freq: {xtal_name} ({header['xtal_freq']})")
    print(f"  MAC: {':'.join(f'{b:02X}' for b in header['mac'])}")
    print(f"  eFuse Size: {header['efuse_size']} bytes")
    print(f"  Flash Size: {header['flash_size']} bytes ({header['flash_size']/1024/1024:.1f} MB)")
    print(f"  Flash MD5: {flash_md5}")
    print()

    # Print encryption info
    print(f"Encryption:")
    print(f"  Key Length: {key_len * 8} bits ({key_len} bytes)")
    print(f"  Key MD5: {key_md5}")
    print()

    # Verify each segment
    print("Verify (decrypt and compare):")
    all_passed = True
    for addr, filepath, bin_data, bin_md5, error in segment_info:
        if error:
            print(f"  [FAIL] 0x{addr:08X} {filepath} - {error}")
            all_passed = False
            continue

        bin_size = len(bin_data)

        # Extract encrypted segment from flash
        encrypted_segment = flash_data[addr:addr + bin_size]

        # Decrypt the segment
        try:
            decrypted_segment = decrypt_flash_data(encrypted_segment, key, addr)
        except Exception as e:
            print(f"  [FAIL] 0x{addr:08X} {filepath} - Decryption error: {e}")
            all_passed = False
            continue

        # Compare decrypted data with original plaintext
        if decrypted_segment == bin_data:
            print(f"  [PASS] 0x{addr:08X} {filepath} ({bin_size} bytes)")
        else:
            # Find first difference
            for i in range(min(len(decrypted_segment), len(bin_data))):
                if decrypted_segment[i] != bin_data[i]:
                    print(f"  [FAIL] 0x{addr:08X} {filepath} ({bin_size} bytes)")
                    print(f"         First diff at offset 0x{addr + i:08X}: decrypted=0x{decrypted_segment[i]:02X} expected=0x{bin_data[i]:02X}")
                    break
            all_passed = False

    print()
    if all_passed:
        print("All encrypted flash segments verified successfully.")
    else:
        print("ERROR: Some encrypted flash segments failed verification.")

    return all_passed


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <flash_dir> <device_file>")
        print()
        print("Arguments:")
        print("  flash_dir    - Directory containing flash_args and binary files (plaintext)")
        print("  device_file  - FakeEsptool .esp device file to verify")
        print()
        print("Example:")
        print(f"  {sys.argv[0]} build/my_project my_device.esp")
        print()
        print("Note: This script verifies encrypted flash data by decrypting it using")
        print("      the key from eFuse and comparing with the original plaintext files.")
        print("      Use verify_flash.py for non-encrypted flash verification.")
        sys.exit(1)

    flash_dir = sys.argv[1]
    device_file = sys.argv[2]

    if not os.path.isdir(flash_dir):
        print(f"ERROR: Flash directory not found: {flash_dir}")
        sys.exit(1)

    if not os.path.isfile(device_file):
        print(f"ERROR: Device file not found: {device_file}")
        sys.exit(1)

    try:
        success = verify_encrypted_flash(flash_dir, device_file)
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
