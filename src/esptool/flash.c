/*
 * flash.c - Flash storage simulation implementation
 *
 * Provides read/write/erase operations on simulated flash memory.
 */

#include "flash.h"
#include "../utils/trace.h"
#include <wincrypt.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

#if ENABLE_TRACE
static const char *TAG = "FLASH";
#endif

BOOL Flash_Init(FLASH_CTX *ctx, DWORD size)
{
    ctx->data = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!ctx->data) {
        TRACE_FW(TAG, "Failed to allocate %lu bytes", size);
        return FALSE;
    }

    memset(ctx->data, FLASH_ERASE_PATTERN, size);
    ctx->size = size;
    ctx->allocated = TRUE;

    TRACE_FW(TAG, "Initialized %lu KB flash", size / 1024);
    return TRUE;
}

void Flash_Close(FLASH_CTX *ctx)
{
    if (ctx->allocated && ctx->data) {
        HeapFree(GetProcessHeap(), 0, ctx->data);
        ctx->data = NULL;
        ctx->allocated = FALSE;
    }
}

BOOL Flash_Read(const FLASH_CTX *ctx, DWORD addr, BYTE *buf, DWORD len)
{
    if (!ctx->data || addr + len > ctx->size)
        return FALSE;

    memcpy(buf, ctx->data + addr, len);
    return TRUE;
}

BOOL Flash_Write(FLASH_CTX *ctx, DWORD addr, const BYTE *data, DWORD len)
{
    if (!ctx->data || addr + len > ctx->size)
        return FALSE;

    for (DWORD i = 0; i < len; i++)
        ctx->data[addr + i] &= data[i];

    return TRUE;
}

BOOL Flash_Erase(FLASH_CTX *ctx, DWORD addr, DWORD len)
{
    if (!ctx->data || addr + len > ctx->size)
        return FALSE;

    memset(ctx->data + addr, FLASH_ERASE_PATTERN, len);
    return TRUE;
}

BOOL Flash_EraseAll(FLASH_CTX *ctx)
{
    if (!ctx->data)
        return FALSE;

    memset(ctx->data, FLASH_ERASE_PATTERN, ctx->size);
    return TRUE;
}

void Flash_CalcMd5(const FLASH_CTX *ctx, DWORD addr, DWORD len, BYTE md5[16])
{
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    memset(md5, 0, 16);

    if (!ctx->data || addr + len > ctx->size)
        return;

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return;

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return;
    }

    CryptHashData(hHash, ctx->data + addr, len, 0);

    DWORD hashLen = 16;
    CryptGetHashParam(hHash, HP_HASHVAL, md5, &hashLen, 0);

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}
