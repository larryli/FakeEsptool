/*
 * flash.h - Flash storage simulation
 *
 * Provides read/write/erase operations on simulated flash memory.
 */

#ifndef ESP_FLASH_H
#define ESP_FLASH_H

#include <stdint.h>
#include <stdbool.h>

/* Erase pattern (all ones) */
#define FLASH_ERASE_PATTERN 0xFF

/* Flash sector size (4KB) - all erase operations are sector-aligned */
#define FLASH_SECTOR_SIZE 4096

/* Flash storage context */
typedef struct {
    uint8_t *data; /* Flash data buffer */
    uint32_t size; /* Flash size in bytes */
} FLASH_CTX;

/*
 * Flash_Init - Initialize flash with specified size
 */
bool Flash_Init(FLASH_CTX *ctx, uint32_t size);

/*
 * Flash_Close - Release flash resources
 */
void Flash_Close(FLASH_CTX *ctx);

/*
 * Flash_Read - Read data from flash
 */
bool Flash_Read(const FLASH_CTX *ctx, uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * Flash_Write - Write data to flash (AND operation)
 *
 * Simulates real Flash behavior: flash can only change bits from 1 to 0,
 * not 0 to 1. To change 0 to 1, the sector must be erased first (set to 0xFF).
 * This function performs: flash[i] &= data[i]
 */
bool Flash_Write(FLASH_CTX *ctx, uint32_t addr, const uint8_t *data, uint32_t len);

/*
 * Flash_Erase - Erase flash region (set to 0xFF)
 */
bool Flash_Erase(FLASH_CTX *ctx, uint32_t addr, uint32_t len);

/*
 * Flash_EraseAll - Erase entire flash
 */
bool Flash_EraseAll(FLASH_CTX *ctx);

/*
 * Flash_CalcMd5 - Calculate MD5 hash of flash region
 */
void Flash_CalcMd5(const FLASH_CTX *ctx, uint32_t addr, uint32_t len, uint8_t md5[16]);

#endif
