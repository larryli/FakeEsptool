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
 * Returns true on success, false on failure (invalid size or memory error).
 */
bool Flash_Init(FLASH_CTX *ctx, uint32_t size)
{
    if (size == 0) {
        return false;
    }

    ctx->data = (uint8_t *)EsptoolHal_MemAlloc(size);
    if (!ctx->data) {
        EsptoolHal_LogD(TAG, "Failed to allocate %lu bytes", size);
        return false;
    }

    memset(ctx->data, FLASH_ERASE_PATTERN, size);
    ctx->size = size;

    EsptoolHal_LogD(TAG, "Initialized %lu KB flash", size / 1024);
    return true;
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
 * Returns true on success, false if address range is invalid.
 */
bool Flash_Read(const FLASH_CTX *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return false;
    }

    memcpy(buf, ctx->data + addr, len);
    return true;
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
 * Returns true on success, false if address range is invalid.
 */
bool Flash_Write(FLASH_CTX *ctx, uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return false;
    }

    for (uint32_t i = 0; i < len; i++) {
        ctx->data[addr + i] &= data[i];
    }

    return true;
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
 * Returns true on success, false if parameters are invalid.
 */
bool Flash_Erase(FLASH_CTX *ctx, uint32_t addr, uint32_t len)
{
    if (!ctx->data || len == 0 || addr >= ctx->size) {
        return false;
    }

    /* Align to sector boundaries (4KB) */
    uint32_t start_sector = (addr / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    uint32_t end_addr = addr + len;
    uint32_t end_sector =
        ((end_addr + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) *
        FLASH_SECTOR_SIZE;

    /* Clamp to flash size */
    if (end_sector > ctx->size) {
        end_sector = ctx->size;
    }

    uint32_t aligned_len = end_sector - start_sector;

    EsptoolHal_LogD(TAG, "Erase: addr=0x%08lX len=%lu -> aligned: 0x%08lX len=%lu",
                addr, len, start_sector, aligned_len);

    memset(ctx->data + start_sector, FLASH_ERASE_PATTERN, aligned_len);
    return true;
}

/*
 * Flash_EraseAll - Erase entire flash
 *
 * Sets all flash bytes to 0xFF.
 */
bool Flash_EraseAll(FLASH_CTX *ctx)
{
    if (!ctx->data) {
        return false;
    }

    memset(ctx->data, FLASH_ERASE_PATTERN, ctx->size);
    return true;
}

/*
 * Flash_CalcMd5 - Calculate MD5 hash of flash region
 *
 * @ctx:  Pointer to flash context (const, read-only)
 * @addr: Start address in flash
 * @len:  Number of bytes to hash
 * @md5:  Buffer to receive 16-uint8_t MD5 hash
 */
void Flash_CalcMd5(const FLASH_CTX *ctx, uint32_t addr, uint32_t len, uint8_t md5[16])
{
    memset(md5, 0, 16);

    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return;
    }

    EsptoolHal_MD5Calc(ctx->data + addr, len, md5);
}
