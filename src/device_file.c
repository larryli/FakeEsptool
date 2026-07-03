/*
 * device_file.c - .esp device file format implementation
 *
 * Handles save/load of device configuration files.
 * Extracted from esptool/device.c - platform I/O code, not simulation logic.
 */

#include "device_file.h"
#include "fesptool/efuse.h"
#include "utils/trace.h"
#include <string.h>

#if ENABLE_TRACE
static const char *TAG = "DEV";
#endif

/* Block descriptor: offset in efuse array and word count (32-bit words) */
typedef struct {
    uint16_t offset;
    uint8_t words;
} efuse_block_desc_t;

/* ESP32 blocks: 4 blocks */
static const efuse_block_desc_t blocks_esp32[] = {
    {0x000, 7}, {0x038, 8}, {0x058, 8}, {0x078, 8},
};

/* ESP32-C2 blocks: 4 blocks */
static const efuse_block_desc_t blocks_c2[] = {
    {0x02C, 2}, {0x034, 3}, {0x040, 8}, {0x060, 8},
};

/* S2/S3/C3/C5/C6/C61/H2/P4 blocks: 11 blocks */
static const efuse_block_desc_t blocks_new_arch[] = {
    {0x02C, 6}, {0x044, 6}, {0x05C, 8}, {0x07C, 8},
    {0x09C, 8}, {0x0BC, 8}, {0x0DC, 8}, {0x0FC, 8},
    {0x11C, 8}, {0x13C, 8}, {0x15C, 8},
};

/* ESP32-S31 blocks: 10 blocks (no KEY5) */
static const efuse_block_desc_t blocks_s31[] = {
    {0x02C, 9}, {0x050, 6}, {0x068, 8}, {0x088, 8},
    {0x0A8, 8}, {0x0C8, 8}, {0x0E8, 8}, {0x108, 8},
    {0x128, 8}, {0x148, 8},
};

static const efuse_block_desc_t *get_block_desc(fesp_chip_type_t type,
                                                 int *count)
{
    switch (type) {
    case FESP_CHIP_ESP32:
        *count = 4;
        return blocks_esp32;
    case FESP_CHIP_ESP32C2:
        *count = 4;
        return blocks_c2;
    case FESP_CHIP_ESP32S31:
        *count = 10;
        return blocks_s31;
    default:
        *count = 11;
        return blocks_new_arch;
    }
}

/*
 * eFuse block data sizes per chip (QEMU-compatible packed format)
 */
static const int efuse_block_sizes[FESP_CHIP_COUNT] = {
    [FESP_CHIP_ESP8266] = 0,
    [FESP_CHIP_ESP32] = 124,
    [FESP_CHIP_ESP32S2] = 336,
    [FESP_CHIP_ESP32S3] = 336,
    [FESP_CHIP_ESP32C2] = 84,
    [FESP_CHIP_ESP32C3] = 336,
    [FESP_CHIP_ESP32C6] = 336,
    [FESP_CHIP_ESP32C5] = 336,
    [FESP_CHIP_ESP32C61] = 336,
    [FESP_CHIP_ESP32H2] = 336,
    [FESP_CHIP_ESP32P4] = 336,
    [FESP_CHIP_ESP32S31] = 296,
};

int DeviceFile_GetEfuseBlockSize(fesp_chip_type_t type)
{
    if (type < FESP_CHIP_COUNT)
        return efuse_block_sizes[type];
    return 0;
}

static void extract_efuse_blocks(const uint8_t *efuse, int efuse_size,
                                 uint8_t *out, fesp_chip_type_t type)
{
    int count;
    const efuse_block_desc_t *desc = get_block_desc(type, &count);
    int dst = 0;
    for (int i = 0; i < count; i++) {
        int src_ofs = desc[i].offset;
        int bytes = desc[i].words * 4;
        if (src_ofs + bytes <= efuse_size) {
            memcpy(out + dst, efuse + src_ofs, bytes);
        }
        dst += bytes;
    }
}

static void inject_efuse_blocks(uint8_t *efuse, int efuse_size,
                                const uint8_t *in, fesp_chip_type_t type)
{
    int count;
    const efuse_block_desc_t *desc = get_block_desc(type, &count);
    int src = 0;
    for (int i = 0; i < count; i++) {
        int dst_ofs = desc[i].offset;
        int bytes = desc[i].words * 4;
        if (dst_ofs + bytes <= efuse_size) {
            memcpy(efuse + dst_ofs, in + src, bytes);
        }
        src += bytes;
    }
}

BOOL DeviceFile_ExportEfuseBlocks(const fesp_chip_ctx_t *chip, uint8_t *out,
                                  int outSize)
{
    if (!chip || !chip->efuse || !out)
        return FALSE;
    if (chip->type == FESP_CHIP_ESP8266)
        return FALSE;
    int blockSize = DeviceFile_GetEfuseBlockSize(chip->type);
    if (blockSize <= 0 || outSize < blockSize)
        return FALSE;
    extract_efuse_blocks(chip->efuse, chip->efuse_size, out, chip->type);
    return TRUE;
}

BOOL DeviceFile_ImportEfuseBlocks(fesp_chip_ctx_t *chip, const uint8_t *data,
                                  int dataSize)
{
    if (!chip || !chip->efuse || !data)
        return FALSE;
    if (chip->type == FESP_CHIP_ESP8266)
        return FALSE;
    int blockSize = DeviceFile_GetEfuseBlockSize(chip->type);
    if (blockSize <= 0 || dataSize != blockSize)
        return FALSE;
    inject_efuse_blocks(chip->efuse, chip->efuse_size, data, chip->type);
    return TRUE;
}

/*
 * DeviceFile_Save - Save device state to .esp file
 *
 * Writes chip config, eFuse block data, and flash data to binary file.
 * ESP8266: always saves full efuse array (v1 style).
 * Others: saves packed block data (v2), compatible with QEMU/esp-emulator.
 * On failure, deletes the partially written file.
 */
BOOL DeviceFile_Save(fesp_chip_ctx_t *chip, fesp_flash_ctx_t *flash,
                     const WCHAR *filename)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        TRACE_PROTO(TAG, "CreateFile failed: %lu", GetLastError());
        return FALSE;
    }

    DWORD written;
    DWORD magic = DEVICE_FILE_MAGIC;
    BOOL is_esp8266 = (chip->type == FESP_CHIP_ESP8266);
    DWORD version = is_esp8266 ? 1 : DEVICE_FILE_VERSION;
    DWORD chipType = (DWORD)chip->type;
    BYTE xtalFreq = chip->xtal_freq;
    BYTE reserved3[3] = {0};
    DWORD flashSize = flash->size;
    DWORD efuseSize;
    BOOL ok = TRUE;

    if (is_esp8266) {
        efuseSize = (DWORD)chip->efuse_size;
    } else {
        efuseSize = (DWORD)DeviceFile_GetEfuseBlockSize(chip->type);
    }

    /* Header (16 bytes) */
    ok = ok && WriteFile(hFile, &magic, 4, &written, NULL) && written == 4;
    ok = ok && WriteFile(hFile, &version, 4, &written, NULL) && written == 4;
    ok = ok && WriteFile(hFile, &chipType, 4, &written, NULL) && written == 4;
    ok = ok && WriteFile(hFile, &xtalFreq, 1, &written, NULL) && written == 1;
    ok = ok && WriteFile(hFile, reserved3, 3, &written, NULL) && written == 3;

    /* MAC (8 bytes) */
    ok = ok && WriteFile(hFile, chip->mac, 6, &written, NULL) && written == 6;
    ok = ok && WriteFile(hFile, reserved3, 2, &written, NULL) && written == 2;

    /* Flash config (4 bytes) */
    ok = ok && WriteFile(hFile, &flashSize, 4, &written, NULL) && written == 4;

    /* eFuse (variable) */
    ok = ok && WriteFile(hFile, &efuseSize, 4, &written, NULL) && written == 4;
    if (efuseSize > 0 && chip->efuse) {
        if (is_esp8266) {
            ok = ok && WriteFile(hFile, chip->efuse, efuseSize, &written,
                                 NULL) &&
                 written == efuseSize;
        } else {
            uint8_t *blockBuf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0,
                                                      efuseSize);
            if (blockBuf) {
                extract_efuse_blocks(chip->efuse, chip->efuse_size, blockBuf,
                                     chip->type);
                ok = ok && WriteFile(hFile, blockBuf, efuseSize, &written,
                                     NULL) &&
                     written == efuseSize;
                HeapFree(GetProcessHeap(), 0, blockBuf);
            } else {
                ok = FALSE;
            }
        }
    }

    /* Flash data (variable) */
    if (flashSize > 0) {
        if (!flash->data) {
            TRACE_PROTO(TAG, "Device save failed: flash data is NULL");
            CloseHandle(hFile);
            DeleteFileW(filename);
            return FALSE;
        }
        ok = ok && WriteFile(hFile, flash->data, flashSize, &written, NULL) &&
             written == flashSize;
    }

    CloseHandle(hFile);

    if (!ok) {
        TRACE_PROTO(TAG, "Device save failed: write error");
        DeleteFileW(filename);
        return FALSE;
    }

    TRACE_PROTO(TAG, "Device saved: %S (v%lu)", filename, version);
    return TRUE;
}

/*
 * DeviceFile_Load - Load device state from .esp file
 *
 * Reads chip config, eFuse data, and flash data from binary file.
 * Validates magic number and version. v1: full efuse array.
 * v2: packed block data (QEMU-compatible). ESP8266 always uses v1.
 * Calls fesp_chip_close/fesp_flash_close internally before re-initializing.
 */
BOOL DeviceFile_Load(fesp_chip_ctx_t *chip, fesp_flash_ctx_t *flash,
                     const WCHAR *filename)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        TRACE_PROTO(TAG, "CreateFile failed: %lu", GetLastError());
        return FALSE;
    }

    DWORD read;
    DWORD magic, version, chipType, flashSize, efuseSize;
    BYTE xtalFreq;
    BYTE headerReserved[3];
    BYTE macReserved[2];
    BYTE mac[6];
    BOOL ok = TRUE;

    /* Header (16 bytes) */
    ok = ok && ReadFile(hFile, &magic, 4, &read, NULL) && read == 4;
    if (!ok || magic != DEVICE_FILE_MAGIC) {
        CloseHandle(hFile);
        TRACE_PROTO(TAG, "Invalid magic");
        return FALSE;
    }

    ok = ok && ReadFile(hFile, &version, 4, &read, NULL) && read == 4;
    if (!ok || (version != 1 && version != DEVICE_FILE_VERSION)) {
        CloseHandle(hFile);
        TRACE_PROTO(TAG, "Unsupported version: %lu", version);
        return FALSE;
    }

    ok = ok && ReadFile(hFile, &chipType, 4, &read, NULL) && read == 4;
    if (!ok || chipType >= FESP_CHIP_COUNT) {
        CloseHandle(hFile);
        TRACE_PROTO(TAG, "Invalid chip type: %lu", chipType);
        return FALSE;
    }

    ok = ok && ReadFile(hFile, &xtalFreq, 1, &read, NULL) && read == 1;
    ok = ok && ReadFile(hFile, headerReserved, 3, &read, NULL) && read == 3;

    /* MAC (8 bytes) */
    ok = ok && ReadFile(hFile, mac, 6, &read, NULL) && read == 6;
    ok = ok && ReadFile(hFile, macReserved, 2, &read, NULL) && read == 2;

    /* Flash config (4 bytes) */
    ok = ok && ReadFile(hFile, &flashSize, 4, &read, NULL) && read == 4;

    if (!ok) {
        CloseHandle(hFile);
        TRACE_PROTO(TAG, "Failed to read header");
        return FALSE;
    }

    /* Close existing resources before re-init */
    fesp_flash_close(flash);
    fesp_chip_close(chip);

    if (!fesp_chip_init(chip, (fesp_chip_type_t)chipType)) {
        CloseHandle(hFile);
        return FALSE;
    }
    fesp_chip_set_flash_size(chip, flashSize);
    fesp_chip_set_mac(chip, mac);
    chip->xtal_freq = xtalFreq;

    /* eFuse (variable) */
    ok = ReadFile(hFile, &efuseSize, 4, &read, NULL) && read == 4;
    if (!ok) {
        CloseHandle(hFile);
        TRACE_PROTO(TAG, "Failed to read efuse size");
        fesp_chip_close(chip);
        return FALSE;
    }

    if (efuseSize > 0) {
        if (version == 1 || chipType == FESP_CHIP_ESP8266) {
            /* v1: read full efuse array directly */
            if (efuseSize <= (DWORD)chip->efuse_size) {
                ok = ReadFile(hFile, chip->efuse, efuseSize, &read, NULL) &&
                     read == efuseSize;
            } else {
                SetFilePointer(hFile, efuseSize, NULL, FILE_CURRENT);
                TRACE_PROTO(TAG,
                            "Warning: efuse size mismatch (file=%lu, chip=%d)",
                            efuseSize, chip->efuse_size);
            }
        } else {
            /* v2: read packed block data and unpack into efuse array */
            int blockSize = DeviceFile_GetEfuseBlockSize(chip->type);
            if (blockSize > 0 && efuseSize == (DWORD)blockSize) {
                uint8_t *blockBuf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0,
                                                          efuseSize);
                if (blockBuf) {
                    ok = ReadFile(hFile, blockBuf, efuseSize, &read, NULL) &&
                         read == efuseSize;
                    if (ok) {
                        int count;
                        const efuse_block_desc_t *desc =
                            get_block_desc(chip->type, &count);
                        int src = 0;
                        for (int i = 0; i < count; i++) {
                            int dst_ofs = desc[i].offset;
                            int bytes = desc[i].words * 4;
                            if (dst_ofs + bytes <= chip->efuse_size) {
                                memcpy(chip->efuse + dst_ofs, blockBuf + src,
                                       bytes);
                            }
                            src += bytes;
                        }
                    }
                    HeapFree(GetProcessHeap(), 0, blockBuf);
                } else {
                    ok = FALSE;
                }
            } else {
                SetFilePointer(hFile, efuseSize, NULL, FILE_CURRENT);
                TRACE_PROTO(TAG,
                            "Warning: v2 efuse size mismatch (file=%lu, "
                            "expected=%d)",
                            efuseSize, blockSize);
            }
        }
        if (!ok) {
            CloseHandle(hFile);
            TRACE_PROTO(TAG, "Failed to read efuse data");
            fesp_chip_close(chip);
            return FALSE;
        }
    }

    fesp_efuse_apply_block0_defaults(chip);

    /* Flash init and data */
    if (!fesp_flash_init(flash, flashSize)) {
        fesp_chip_close(chip);
        CloseHandle(hFile);
        return FALSE;
    }

    if (flashSize > 0) {
        ok = ReadFile(hFile, flash->data, flashSize, &read, NULL) &&
             read == flashSize;
        if (!ok) {
            TRACE_PROTO(TAG,
                        "Failed to read flash data (expected %lu, got %lu)",
                        flashSize, read);
            CloseHandle(hFile);
            fesp_flash_close(flash);
            fesp_chip_close(chip);
            return FALSE;
        }
    }

    CloseHandle(hFile);

    TRACE_PROTO(TAG, "Device loaded: %s, %lu MB (v%lu)", chip->name,
                flashSize / (1024 * 1024), version);
    return TRUE;
}
