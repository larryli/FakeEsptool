/*
 * flash.c - Flash storage simulation implementation
 *
 * Provides read/write/erase operations on simulated flash memory.
 */

#include "../fesptool_hal.h"
#include "flash.h"
#include <string.h>

#if ENABLE_TRACE
static const char *TAG = "FLASH";
#endif

/*
 * fesp_flash_init - Initialize flash storage
 *
 * Allocates memory for flash data and initializes to erased state (0xFF).
 *
 * @ctx:  Pointer to flash context
 * @size: Flash size in bytes
 *
 * Returns true on success, false on failure (invalid size or memory error).
 */
bool fesp_flash_init(fesp_flash_ctx_t *ctx, uint32_t size)
{
    if (size == 0) {
        return false;
    }

    ctx->data = (uint8_t *)fesp_hal_mem_alloc(size);
    if (!ctx->data) {
        FESP_HAL_LOGD(TAG, "Failed to allocate %lu bytes", size);
        return false;
    }

    memset(ctx->data, FESP_FLASH_ERASE_PATTERN, size);
    ctx->size = size;

    FESP_HAL_LOGD(TAG, "Initialized %lu KB flash", size / 1024);
    return true;
}

/*
 * fesp_flash_close - Release flash resources
 *
 * Frees the flash data buffer. Safe to call multiple times.
 */
void fesp_flash_close(fesp_flash_ctx_t *ctx)
{
    if (ctx->data) {
        fesp_hal_mem_free(ctx->data);
        ctx->data = NULL;
    }
}

/*
 * fesp_flash_read - Read data from flash
 *
 * @ctx:  Pointer to flash context (const, read-only)
 * @addr: Start address in flash
 * @buf:  Buffer to receive data
 * @len:  Number of bytes to read
 *
 * Returns true on success, false if address range is invalid.
 */
bool fesp_flash_read(const fesp_flash_ctx_t *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return false;
    }

    memcpy(buf, ctx->data + addr, len);
    return true;
}

/*
 * fesp_flash_write - Write data to flash (AND operation)
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
bool fesp_flash_write(fesp_flash_ctx_t *ctx, uint32_t addr, const uint8_t *data, uint32_t len)
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
 * fesp_flash_erase - Erase flash region
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
bool fesp_flash_erase(fesp_flash_ctx_t *ctx, uint32_t addr, uint32_t len)
{
    if (!ctx->data || len == 0 || addr >= ctx->size) {
        return false;
    }

    /* Align to sector boundaries (4KB) */
    uint32_t start_sector = (addr / FESP_FLASH_SECTOR_SIZE) * FESP_FLASH_SECTOR_SIZE;
    uint32_t end_addr = addr + len;
    uint32_t end_sector =
        ((end_addr + FESP_FLASH_SECTOR_SIZE - 1) / FESP_FLASH_SECTOR_SIZE) *
        FESP_FLASH_SECTOR_SIZE;

    /* Clamp to flash size */
    if (end_sector > ctx->size) {
        end_sector = ctx->size;
    }

    uint32_t aligned_len = end_sector - start_sector;

    FESP_HAL_LOGD(TAG, "Erase: addr=0x%08lX len=%lu -> aligned: 0x%08lX len=%lu",
                addr, len, start_sector, aligned_len);

    memset(ctx->data + start_sector, FESP_FLASH_ERASE_PATTERN, aligned_len);
    return true;
}

/*
 * fesp_flash_erase_all - Erase entire flash
 *
 * Sets all flash bytes to 0xFF.
 */
bool fesp_flash_erase_all(fesp_flash_ctx_t *ctx)
{
    if (!ctx->data) {
        return false;
    }

    memset(ctx->data, FESP_FLASH_ERASE_PATTERN, ctx->size);
    return true;
}

/*
 * fesp_flash_calc_md5 - Calculate MD5 hash of flash region
 *
 * @ctx:  Pointer to flash context (const, read-only)
 * @addr: Start address in flash
 * @len:  Number of bytes to hash
 * @md5:  Buffer to receive 16-uint8_t MD5 hash
 */
void fesp_flash_calc_md5(const fesp_flash_ctx_t *ctx, uint32_t addr, uint32_t len, uint8_t md5[16])
{
    memset(md5, 0, 16);

    if (!ctx->data || addr >= ctx->size || len > ctx->size - addr) {
        return;
    }

    fesp_hal_md5_calc(ctx->data + addr, len, md5);
}
