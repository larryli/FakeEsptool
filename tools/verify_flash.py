#!/usr/bin/env python3
"""
verify_flash.py - Verify flash data in FakeEsptool device file

Compares flash binary files against a saved .esp device file to verify
that the flash data was correctly written.

Usage:
    python3 verify_flash.py <flash_dir> <device_file>

Arguments:
    flash_dir    - Directory containing flash_args and binary files
    device_file  - FakeEsptool .esp device file to verify

Example:
    python3 verify_flash.py build/my_project my_device.esp

Flash Args Format:
    First line: --flash-mode dio --flash-freq 60m --flash-size 2MB
    Subsequent lines: <address> <filepath>
    Example:
        0x0 bootloader/bootloader.bin
        0x10000 my_app.bin
        0x8000 partition_table/partition-table.bin
"""

import hashlib
import struct
import sys
import os

# Device file constants
DEVICE_MAGIC = 0x45535000  # "ESP\0"
# Device file format version: ESP8266 = 1, all other chips = 2
DEVICE_VERSION_MIN = 1
DEVICE_VERSION_MAX = 2

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
    7: "ESP32-C5",
    8: "ESP32-C61",
    9: "ESP32-H2",
    10: "ESP32-P4",
    11: "ESP32-S31",
}

# Crystal frequency mapping
XTAL_FREQS = {
    0: "40MHz",
    1: "26MHz",
}


def read_device_header(f):
    """Read and parse device file header."""
    magic = struct.unpack('<I', f.read(4))[0]
    if magic != DEVICE_MAGIC:
        raise ValueError(f"Invalid magic: 0x{magic:08X} (expected 0x{DEVICE_MAGIC:08X})")

    version = struct.unpack('<I', f.read(4))[0]
    if version < DEVICE_VERSION_MIN or version > DEVICE_VERSION_MAX:
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


def read_flash_data(f, efuse_size, flash_size):
    """Skip eFuse data and read flash data."""
    # Skip eFuse data
    f.seek(HEADER_SIZE + efuse_size)

    # Read flash data
    flash_data = f.read(flash_size)
    if len(flash_data) != flash_size:
        raise ValueError(f"Failed to read flash data: expected {flash_size} bytes, got {len(flash_data)}")

    return flash_data


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


def verify_flash(flash_dir, device_file):
    """Verify flash data in device file."""
    # Parse flash_args
    segments = parse_flash_args(flash_dir)
    if not segments:
        print("ERROR: No flash segments found in flash_args")
        return False

    # Read entire device file for MD5
    device_data = read_device_file(device_file)
    device_file_md5 = calc_md5(device_data)

    # Read device file header
    with open(device_file, 'rb') as f:
        header = read_device_header(f)
        flash_data = read_flash_data(f, header['efuse_size'], header['flash_size'])

    # Calculate device flash MD5
    flash_md5 = calc_md5(flash_data)

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

    # Verify each segment
    print("Verify:")
    all_passed = True
    for addr, filepath, bin_data, bin_md5, error in segment_info:
        if error:
            print(f"  [FAIL] 0x{addr:08X} {filepath} - {error}")
            all_passed = False
            continue

        bin_size = len(bin_data)
        flash_segment = flash_data[addr:addr + bin_size]

        if flash_segment == bin_data:
            print(f"  [PASS] 0x{addr:08X} {filepath} ({bin_size} bytes)")
        else:
            for i in range(bin_size):
                if flash_segment[i] != bin_data[i]:
                    print(f"  [FAIL] 0x{addr:08X} {filepath} ({bin_size} bytes)")
                    print(f"         First diff at offset 0x{addr + i:08X}: flash=0x{flash_segment[i]:02X} expected=0x{bin_data[i]:02X}")
                    break
            all_passed = False

    print()
    if all_passed:
        print("All flash segments verified successfully.")
    else:
        print("ERROR: Some flash segments failed verification.")

    return all_passed


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <flash_dir> <device_file>")
        print()
        print("Arguments:")
        print("  flash_dir    - Directory containing flash_args and binary files")
        print("  device_file  - FakeEsptool .esp device file to verify")
        print()
        print("Example:")
        print(f"  {sys.argv[0]} build/my_project my_device.esp")
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
        success = verify_flash(flash_dir, device_file)
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
