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

BOOL Device_Init(DEVICE_CTX *ctx, CHIP_TYPE chipType, DWORD flashSize, const BYTE mac[6])
{
    memset(ctx, 0, sizeof(DEVICE_CTX));

    if (!Chip_Init(&ctx->chip, chipType)) {
        TRACE_FW(TAG, "Chip_Init failed");
        return FALSE;
    }

    if (mac)
        Chip_SetMac(&ctx->chip, mac);

    if (!Flash_Init(&ctx->flash, flashSize)) {
        TRACE_FW(TAG, "Flash_Init failed");
        Chip_Close(&ctx->chip);
        return FALSE;
    }

    ctx->modified = TRUE;
    TRACE_FW(TAG, "Device created: %s, %lu MB", ctx->chip.name, flashSize / (1024*1024));
    return TRUE;
}

void Device_Close(DEVICE_CTX *ctx)
{
    Flash_Close(&ctx->flash);
    Chip_Close(&ctx->chip);
    ctx->filename[0] = 0;
    ctx->modified = FALSE;
}

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
    DWORD efuseSize = (DWORD)ctx->chip.efuse_size;
    DWORD flashSize = ctx->flash.size;

    WriteFile(hFile, &magic, 4, &written, NULL);
    WriteFile(hFile, &version, 4, &written, NULL);
    WriteFile(hFile, &chipType, 4, &written, NULL);
    WriteFile(hFile, ctx->chip.mac, 6, &written, NULL);
    WriteFile(hFile, &efuseSize, 4, &written, NULL);
    WriteFile(hFile, ctx->chip.efuse, efuseSize, &written, NULL);
    WriteFile(hFile, &flashSize, 4, &written, NULL);
    WriteFile(hFile, ctx->flash.data, flashSize, &written, NULL);

    CloseHandle(hFile);

    lstrcpyW(ctx->filename, filename);
    ctx->modified = FALSE;

    TRACE_FW(TAG, "Device saved: %S", filename);
    return TRUE;
}

BOOL Device_Load(DEVICE_CTX *ctx, const WCHAR *filename)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, 0, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        TRACE_FW(TAG, "CreateFile failed: %lu", GetLastError());
        return FALSE;
    }

    DWORD read;
    DWORD magic, version, chipType, efuseSize, flashSize;
    BYTE mac[6];

    ReadFile(hFile, &magic, 4, &read, NULL);
    if (magic != DEVICE_MAGIC) {
        CloseHandle(hFile);
        TRACE_FW(TAG, "Invalid magic");
        return FALSE;
    }

    ReadFile(hFile, &version, 4, &read, NULL);
    if (version != DEVICE_VERSION) {
        CloseHandle(hFile);
        TRACE_FW(TAG, "Unsupported version: %lu", version);
        return FALSE;
    }

    ReadFile(hFile, &chipType, 4, &read, NULL);
    if (chipType >= CHIP_COUNT) {
        CloseHandle(hFile);
        TRACE_FW(TAG, "Invalid chip type: %lu", chipType);
        return FALSE;
    }

    ReadFile(hFile, mac, 6, &read, NULL);
    ReadFile(hFile, &efuseSize, 4, &read, NULL);

    Device_Close(ctx);

    if (!Chip_Init(&ctx->chip, (CHIP_TYPE)chipType)) {
        CloseHandle(hFile);
        return FALSE;
    }
    Chip_SetMac(&ctx->chip, mac);

    if (efuseSize > 0 && efuseSize <= (DWORD)ctx->chip.efuse_size) {
        ReadFile(hFile, ctx->chip.efuse, efuseSize, &read, NULL);
    }

    ReadFile(hFile, &flashSize, 4, &read, NULL);

    if (!Flash_Init(&ctx->flash, flashSize)) {
        Chip_Close(&ctx->chip);
        CloseHandle(hFile);
        return FALSE;
    }

    if (flashSize > 0) {
        ReadFile(hFile, ctx->flash.data, flashSize, &read, NULL);
    }

    CloseHandle(hFile);

    lstrcpyW(ctx->filename, filename);
    ctx->modified = FALSE;

    TRACE_FW(TAG, "Device loaded: %S", filename);
    return TRUE;
}

BOOL Device_IsModified(const DEVICE_CTX *ctx)
{
    return ctx->modified;
}

void Device_SetModified(DEVICE_CTX *ctx, BOOL modified)
{
    ctx->modified = modified;
}

const WCHAR *Device_GetFilename(const DEVICE_CTX *ctx)
{
    return ctx->filename;
}
