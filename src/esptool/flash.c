/*
 * flash.c - Flash storage simulation implementation
 *
 * Provides read/write/erase operations on simulated flash memory.
 */

#include "../esptool_hal.h"
#include "flash.h"
#include <string.h>

#if ENABLE_TRACE
static const char *TAG = "FLASH";
#endif

/*
 * Flash_Init - Initialize flash storage
 *
 * Allocates memory for flash data and initializes to erased state (0xFF).
 *
 * @ctx:  Pointer to flash context
 * @size: Flash size in bytes
 *
 * Returns TRUE on success, FALSE on failure (invalid size or memory error).
 */
BOOL Flash_Init(FLASH_CTX *ctx, DWORD size)
{
    if (size == 0) {
        return FALSE;
    }

    ctx->data = (BYTE *)EsptoolHal_MemAlloc(size);
    if (!ctx->data) {
        EsptoolHal_LogD(TAG, "Failed to allocate %lu bytes", size);
        return FALSE;
    }

    memset(ctx->data, FLASH_ERASE_PATTERN, size);
    ctx->size = size;

    EsptoolHal_LogD(TAG, "Initialized %lu KB flash", size / 1024);
    return TRUE;
}

/*
 * Flash_Close - Release flash resources
 *
 * Frees the flash data buffer. Safe to call multiple times.
 */
void Flash_Close(FLASH_CTX *ctx)
{
    if (ctx->data) {
        EsptoolHal_MemFree(ctx->data);
        ctx->data = NULL;
    }
}

/*
 * Flash_Read - Read data from flash
 *
 * @ctx:  Pointer to flash context (const, read-only)
 * @addr: Start address in flash
 * @buf:  Buffer to receive data
 * @len:  Number of bytes to read
 *
 * Returns TRUE on success, FALSE if address range is invalid.
 */
BOOL Flash_Read(const FLASH_CTX *ctx, DWORD addr, BYTE *buf, DWORD len)
{
    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return FALSE;
    }

    memcpy(buf, ctx->data + addr, len);
    return TRUE;
}

/*
 * Flash_Write - Write data to flash (AND operation)
 *
 * Simulates real Flash behavior: bits can only be changed from 1 to 0,
 * never from 0 to 1. To set bits back to 1, the sector must be erased.
 * Implementation: flash[i] &= data[i]
 *
 * @ctx:  Pointer to flash context
 * @addr: Start address in flash
 * @data: Data to write
 * @len:  Number of bytes to write
 *
 * Returns TRUE on success, FALSE if address range is invalid.
 */
BOOL Flash_Write(FLASH_CTX *ctx, DWORD addr, const BYTE *data, DWORD len)
{
    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return FALSE;
    }

    for (DWORD i = 0; i < len; i++) {
        ctx->data[addr + i] &= data[i];
    }

    return TRUE;
}

/*
 * Flash_Erase - Erase flash region
 *
 * Erases a flash region by setting all bytes to 0xFF.
 * Erase operations are sector-aligned (4KB boundaries).
 *
 * @ctx: Pointer to flash context
 * @addr: Start address (will be aligned to sector boundary)
 * @len:  Number of bytes to erase (will be rounded up to sector boundary)
 *
 * Returns TRUE on success, FALSE if parameters are invalid.
 */
BOOL Flash_Erase(FLASH_CTX *ctx, DWORD addr, DWORD len)
{
    if (!ctx->data || len == 0 || addr >= ctx->size) {
        return FALSE;
    }

    /* Align to sector boundaries (4KB) */
    DWORD start_sector = (addr / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    DWORD end_addr = addr + len;
    DWORD end_sector =
        ((end_addr + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) *
        FLASH_SECTOR_SIZE;

    /* Clamp to flash size */
    if (end_sector > ctx->size) {
        end_sector = ctx->size;
    }

    DWORD aligned_len = end_sector - start_sector;

    EsptoolHal_LogD(TAG, "Erase: addr=0x%08lX len=%lu -> aligned: 0x%08lX len=%lu",
                addr, len, start_sector, aligned_len);

    memset(ctx->data + start_sector, FLASH_ERASE_PATTERN, aligned_len);
    return TRUE;
}

/*
 * Flash_EraseAll - Erase entire flash
 *
 * Sets all flash bytes to 0xFF.
 */
BOOL Flash_EraseAll(FLASH_CTX *ctx)
{
    if (!ctx->data) {
        return FALSE;
    }

    memset(ctx->data, FLASH_ERASE_PATTERN, ctx->size);
    return TRUE;
}

/*
 * Flash_CalcMd5 - Calculate MD5 hash of flash region
 *
 * @ctx:  Pointer to flash context (const, read-only)
 * @addr: Start address in flash
 * @len:  Number of bytes to hash
 * @md5:  Buffer to receive 16-byte MD5 hash
 */
void Flash_CalcMd5(const FLASH_CTX *ctx, DWORD addr, DWORD len, BYTE md5[16])
{
    memset(md5, 0, 16);

    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return;
    }

    EsptoolHal_MD5Calc(ctx->data + addr, len, md5);
}
