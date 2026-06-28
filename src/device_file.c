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

/*
 * DeviceFile_Save - Save device state to .esp file
 *
 * Writes chip config, eFuse data, and flash data to binary file.
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
    DWORD version = DEVICE_FILE_VERSION;
    DWORD chipType = (DWORD)chip->type;
    BYTE xtalFreq = chip->xtal_freq;
    BYTE reserved3[3] = {0};
    DWORD flashSize = flash->size;
    DWORD efuseSize = (DWORD)chip->efuse_size;
    BOOL ok = TRUE;

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
    if (efuseSize > 0 && chip->efuse)
        ok = ok && WriteFile(hFile, chip->efuse, efuseSize, &written, NULL) &&
             written == efuseSize;

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

    TRACE_PROTO(TAG, "Device saved: %S", filename);
    return TRUE;
}

/*
 * DeviceFile_Load - Load device state from .esp file
 *
 * Reads chip config, eFuse data, and flash data from binary file.
 * Validates magic number and version. Calls fesp_chip_close/fesp_flash_close
 * internally before re-initializing with loaded data.
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
    if (!ok || version != DEVICE_FILE_VERSION) {
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
        if (efuseSize <= (DWORD)chip->efuse_size) {
            ok = ReadFile(hFile, chip->efuse, efuseSize, &read, NULL) &&
                 read == efuseSize;
            if (!ok) {
                CloseHandle(hFile);
                TRACE_PROTO(TAG, "Failed to read efuse data");
                fesp_chip_close(chip);
                return FALSE;
            }
        } else {
            SetFilePointer(hFile, efuseSize, NULL, FILE_CURRENT);
            TRACE_PROTO(TAG, "Warning: efuse size mismatch (file=%lu, chip=%d)",
                        efuseSize, chip->efuse_size);
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

    TRACE_PROTO(TAG, "Device loaded: %s, %lu MB", chip->name,
                flashSize / (1024 * 1024));
    return TRUE;
}
