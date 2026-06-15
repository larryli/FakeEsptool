/*
 * device.c - Device file management implementation
 *
 * Handles save/load of device configuration (.esp files).
 */

#include "device.h"
#include "../utils/trace.h"
#include <string.h>

#if ENABLE_TRACE
static const char *TAG = "DEV";
#endif

/*
 * Device_Init - Initialize new device
 *
 * Creates a new device with specified chip type, flash size, and MAC address.
 * Allocates memory for chip eFuse and flash storage.
 *
 * @ctx:       Pointer to device context
 * @chipType:  Chip type enum (CHIP_ESP8266, CHIP_ESP32, etc.)
 * @flashSize: Flash size in bytes
 * @mac:       6-byte MAC address (can be NULL for default)
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL Device_Init(DEVICE_CTX *ctx, CHIP_TYPE chipType, DWORD flashSize, const BYTE mac[6])
{
    memset(ctx, 0, sizeof(DEVICE_CTX));

    if (!Chip_Init(&ctx->chip, chipType)) {
        TRACE_FW(TAG, "Chip_Init failed");
        return FALSE;
    }

    Chip_SetFlashSize(&ctx->chip, flashSize);

    if (mac)
        Chip_SetMac(&ctx->chip, mac);

    if (!Flash_Init(&ctx->flash, flashSize)) {
        TRACE_FW(TAG, "Flash_Init failed");
        Chip_Close(&ctx->chip);
        return FALSE;
    }

    ctx->modified = FALSE;
    TRACE_FW(TAG, "Device created: %s, %lu MB", ctx->chip.name, flashSize / (1024*1024));
    return TRUE;
}

/*
 * Device_Close - Release device resources
 *
 * Frees flash and chip resources. Safe to call multiple times.
 */
void Device_Close(DEVICE_CTX *ctx)
{
    Flash_Close(&ctx->flash);
    Chip_Close(&ctx->chip);
    ctx->filename[0] = 0;
    ctx->modified = FALSE;
}

/*
 * Device_Save - Save device to .esp file
 *
 * Writes device configuration and data to binary file.
 * File format: header + MAC + flash config + eFuse + flash data.
 *
 * @ctx:      Pointer to device context
 * @filename: Path to save file
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL Device_Save(DEVICE_CTX *ctx, const WCHAR *filename)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        TRACE_FW(TAG, "CreateFile failed: %lu", GetLastError());
        return FALSE;
    }

    DWORD written;
    DWORD magic = DEVICE_MAGIC;
    DWORD version = DEVICE_VERSION;
    DWORD chipType = (DWORD)ctx->chip.type;
    BYTE xtalFreq = ctx->chip.xtal_freq;
    BYTE reserved3[3] = {0};
    DWORD flashSize = ctx->flash.size;
    DWORD efuseSize = (DWORD)ctx->chip.efuse_size;
    BOOL ok = TRUE;

    /* Header (16 bytes) */
    ok = ok && WriteFile(hFile, &magic, 4, &written, NULL) && written == 4;
    ok = ok && WriteFile(hFile, &version, 4, &written, NULL) && written == 4;
    ok = ok && WriteFile(hFile, &chipType, 4, &written, NULL) && written == 4;
    ok = ok && WriteFile(hFile, &xtalFreq, 1, &written, NULL) && written == 1;
    ok = ok && WriteFile(hFile, reserved3, 3, &written, NULL) && written == 3;

    /* MAC (8 bytes) */
    ok = ok && WriteFile(hFile, ctx->chip.mac, 6, &written, NULL) && written == 6;
    ok = ok && WriteFile(hFile, reserved3, 2, &written, NULL) && written == 2;

    /* Flash config (4 bytes) */
    ok = ok && WriteFile(hFile, &flashSize, 4, &written, NULL) && written == 4;

    /* eFuse (variable) */
    ok = ok && WriteFile(hFile, &efuseSize, 4, &written, NULL) && written == 4;
    if (efuseSize > 0 && ctx->chip.efuse)
        ok = ok && WriteFile(hFile, ctx->chip.efuse, efuseSize, &written, NULL) && written == efuseSize;

    /* Flash data (variable) */
    if (flashSize > 0) {
        if (!ctx->flash.data) {
            TRACE_FW(TAG, "Device save failed: flash data is NULL");
            CloseHandle(hFile);
            DeleteFileW(filename);
            return FALSE;
        }
        ok = ok && WriteFile(hFile, ctx->flash.data, flashSize, &written, NULL) && written == flashSize;
    }

    CloseHandle(hFile);

    if (!ok) {
        TRACE_FW(TAG, "Device save failed: write error");
        DeleteFileW(filename);
        return FALSE;
    }

    lstrcpyW(ctx->filename, filename);
    ctx->modified = FALSE;

    TRACE_FW(TAG, "Device saved: %S", filename);
    return TRUE;
}

/*
 * Device_Load - Load device from .esp file
 *
 * Reads device configuration and data from binary file.
 * Validates file magic, version, and chip type.
 *
 * @ctx:      Pointer to device context
 * @filename: Path to load file
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL Device_Load(DEVICE_CTX *ctx, const WCHAR *filename)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, 0, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        TRACE_FW(TAG, "CreateFile failed: %lu", GetLastError());
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
    if (!ok || magic != DEVICE_MAGIC) {
        CloseHandle(hFile);
        TRACE_FW(TAG, "Invalid magic");
        return FALSE;
    }

    ok = ok && ReadFile(hFile, &version, 4, &read, NULL) && read == 4;
    if (!ok || version != DEVICE_VERSION) {
        CloseHandle(hFile);
        TRACE_FW(TAG, "Unsupported version: %lu", version);
        return FALSE;
    }

    ok = ok && ReadFile(hFile, &chipType, 4, &read, NULL) && read == 4;
    if (!ok || chipType >= CHIP_COUNT) {
        CloseHandle(hFile);
        TRACE_FW(TAG, "Invalid chip type: %lu", chipType);
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
        TRACE_FW(TAG, "Failed to read header");
        return FALSE;
    }

    Device_Close(ctx);

    if (!Chip_Init(&ctx->chip, (CHIP_TYPE)chipType)) {
        CloseHandle(hFile);
        return FALSE;
    }
    Chip_SetFlashSize(&ctx->chip, flashSize);
    Chip_SetMac(&ctx->chip, mac);
    ctx->chip.xtal_freq = xtalFreq;

    /* eFuse (variable) */
    ok = ReadFile(hFile, &efuseSize, 4, &read, NULL) && read == 4;
    if (!ok) {
        CloseHandle(hFile);
        TRACE_FW(TAG, "Failed to read efuse size");
        Chip_Close(&ctx->chip);
        return FALSE;
    }

    if (efuseSize > 0) {
        if (efuseSize <= (DWORD)ctx->chip.efuse_size) {
            ok = ReadFile(hFile, ctx->chip.efuse, efuseSize, &read, NULL) && read == efuseSize;
            if (!ok) {
                CloseHandle(hFile);
                TRACE_FW(TAG, "Failed to read efuse data");
                Chip_Close(&ctx->chip);
                return FALSE;
            }
        } else {
            /* Skip excess eFuse data */
            SetFilePointer(hFile, efuseSize, NULL, FILE_CURRENT);
            TRACE_FW(TAG, "Warning: efuse size mismatch (file=%lu, chip=%d)", efuseSize, ctx->chip.efuse_size);
        }
    }

    /* Flash init and data */
    if (!Flash_Init(&ctx->flash, flashSize)) {
        Chip_Close(&ctx->chip);
        CloseHandle(hFile);
        return FALSE;
    }

    if (flashSize > 0) {
        ok = ReadFile(hFile, ctx->flash.data, flashSize, &read, NULL) && read == flashSize;
        if (!ok) {
            TRACE_FW(TAG, "Failed to read flash data (expected %lu, got %lu)", flashSize, read);
            CloseHandle(hFile);
            Flash_Close(&ctx->flash);
            Chip_Close(&ctx->chip);
            return FALSE;
        }
    }

    CloseHandle(hFile);

    lstrcpyW(ctx->filename, filename);
    ctx->modified = FALSE;

    TRACE_FW(TAG, "Device loaded: %s, %lu MB", ctx->chip.name, flashSize / (1024*1024));
    return TRUE;
}

/*
 * Device_IsModified - Check if device data has been modified
 *
 * Returns TRUE if device has unsaved changes.
 */
BOOL Device_IsModified(const DEVICE_CTX *ctx)
{
    return ctx->modified;
}

/*
 * Device_SetModified - Set or clear modification flag
 *
 * @ctx:      Pointer to device context
 * @modified: TRUE to mark as modified, FALSE to clear
 */
void Device_SetModified(DEVICE_CTX *ctx, BOOL modified)
{
    ctx->modified = modified;
}

/*
 * Device_GetFilename - Get current file path
 *
 * Returns pointer to file path string, or empty string if no file.
 */
const WCHAR *Device_GetFilename(const DEVICE_CTX *ctx)
{
    return ctx->filename;
}
