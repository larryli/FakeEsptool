#!/usr/bin/env python3
"""
efuse_convert.py - Convert between QEMU/esp-emulator eFuse format and espefuse --virt format.

QEMU/esp-emulator format: packed block read data (124/336 bytes)
espefuse --virt format: full register address space (288/512 bytes)

Usage:
    efuse_convert.py --chip esp32c3 --from qemu --to virt input.bin output.bin
    efuse_convert.py --chip esp32c3 --from virt --to qemu input.bin output.bin
"""

import argparse
import struct
import sys

# Block descriptors: (offset, word_count) for each chip
BLOCK_DESCRIPTORS = {
    "esp32": [
        (0x000, 7), (0x038, 8), (0x058, 8), (0x078, 8),
    ],
    "esp32c2": [
        (0x02C, 2), (0x034, 3), (0x040, 8), (0x060, 8),
    ],
    "esp32s2": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32s3": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32c3": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32c5": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32c6": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32c61": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32h2": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32p4": [
        (0x02C, 6), (0x044, 6), (0x05C, 8), (0x07C, 8),
        (0x09C, 8), (0x0BC, 8), (0x0DC, 8), (0x0FC, 8),
        (0x11C, 8), (0x13C, 8), (0x15C, 8),
    ],
    "esp32s31": [
        (0x02C, 9), (0x050, 6), (0x068, 8), (0x088, 8),
        (0x0A8, 8), (0x0C8, 8), (0x0E8, 8), (0x108, 8),
        (0x128, 8), (0x148, 8),
    ],
}

# QEMU block data sizes
QEMU_SIZES = {
    "esp32": 124,
    "esp32c2": 84,
    "esp32s2": 336, "esp32s3": 336, "esp32c3": 336, "esp32c5": 336,
    "esp32c6": 336, "esp32c61": 336, "esp32h2": 336, "esp32p4": 336,
    "esp32s31": 296,
}

# espefuse --virt register space sizes
VIRT_SIZES = {
    "esp32": 288,
    "esp32c2": 512, "esp32s2": 512, "esp32s3": 512, "esp32c3": 512,
    "esp32c5": 512, "esp32c6": 512, "esp32c61": 512, "esp32h2": 512,
    "esp32p4": 512, "esp32s31": 512,
}


def qemu_to_virt(chip, data):
    """Convert QEMU block data to espefuse --virt register space."""
    blocks = BLOCK_DESCRIPTORS[chip]
    virt_size = VIRT_SIZES[chip]
    virt = bytearray(virt_size)
    src = 0
    for offset, words in blocks:
        nbytes = words * 4
        if src + nbytes <= len(data):
            # QEMU stores little-endian, copy directly
            for i in range(nbytes):
                if offset + i < virt_size:
                    virt[offset + i] = data[src + i]
        src += nbytes
    return bytes(virt)


def virt_to_qemu(chip, data):
    """Convert espefuse --virt register space to QEMU block data."""
    blocks = BLOCK_DESCRIPTORS[chip]
    qemu_size = QEMU_SIZES[chip]
    qemu = bytearray(qemu_size)
    dst = 0
    for offset, words in blocks:
        nbytes = words * 4
        for i in range(nbytes):
            if offset + i < len(data) and dst + i < qemu_size:
                qemu[dst + i] = data[offset + i]
        dst += nbytes
    return bytes(qemu)


def main():
    parser = argparse.ArgumentParser(
        description="Convert between QEMU and espefuse --virt eFuse formats")
    parser.add_argument("--chip", required=True,
                        choices=list(QEMU_SIZES.keys()),
                        help="Target chip type")
    parser.add_argument("--from", dest="src_fmt", required=True,
                        choices=["qemu", "virt"],
                        help="Source format")
    parser.add_argument("--to", dest="dst_fmt", required=True,
                        choices=["qemu", "virt"],
                        help="Destination format")
    parser.add_argument("input", help="Input file path")
    parser.add_argument("output", help="Output file path")
    args = parser.parse_args()

    if args.src_fmt == args.dst_fmt:
        print("Error: source and destination formats are the same")
        sys.exit(1)

    with open(args.input, "rb") as f:
        data = f.read()

    if args.src_fmt == "qemu":
        expected = QEMU_SIZES[args.chip]
        if len(data) != expected:
            print(f"Error: expected {expected} bytes for {args.chip}, got {len(data)}")
            sys.exit(1)
        result = qemu_to_virt(args.chip, data)
    else:
        expected = VIRT_SIZES[args.chip]
        if len(data) != expected:
            print(f"Error: expected {expected} bytes for {args.chip}, got {len(data)}")
            sys.exit(1)
        result = virt_to_qemu(args.chip, data)

    with open(args.output, "wb") as f:
        f.write(result)

    print(f"Converted {args.src_fmt} -> {args.dst_fmt} for {args.chip}")
    print(f"  Input:  {len(data)} bytes")
    print(f"  Output: {len(result)} bytes")


if __name__ == "__main__":
    main()
