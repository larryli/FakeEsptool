/*
 * flash.h - Flash storage simulation
 *
 * Provides read/write/erase operations on simulated flash memory.
 */

#ifndef ESP_FLASH_H
#define ESP_FLASH_H

#include <windows.h>

/* Erase pattern (all ones) */
#define FLASH_ERASE_PATTERN 0xFF

/* Flash sector size (4KB) - all erase operations are sector-aligned */
#define FLASH_SECTOR_SIZE 4096

/* Flash storage context */
typedef struct {
    BYTE *data;             /* Flash data buffer */
    DWORD size;             /* Flash size in bytes */
} FLASH_CTX;

/*
 * Flash_Init - Initialize flash with specified size
 */
BOOL Flash_Init(FLASH_CTX *ctx, DWORD size);

/*
 * Flash_Close - Release flash resources
 */
void Flash_Close(FLASH_CTX *ctx);

/*
 * Flash_Read - Read data from flash
 */
BOOL Flash_Read(const FLASH_CTX *ctx, DWORD addr, BYTE *buf, DWORD len);

/*
 * Flash_Write - Write data to flash (AND operation)
 *
 * Simulates real Flash behavior: flash can only change bits from 1 to 0,
 * not 0 to 1. To change 0 to 1, the sector must be erased first (set to 0xFF).
 * This function performs: flash[i] &= data[i]
 */
BOOL Flash_Write(FLASH_CTX *ctx, DWORD addr, const BYTE *data, DWORD len);

/*
 * Flash_Erase - Erase flash region (set to 0xFF)
 */
BOOL Flash_Erase(FLASH_CTX *ctx, DWORD addr, DWORD len);

/*
 * Flash_EraseAll - Erase entire flash
 */
BOOL Flash_EraseAll(FLASH_CTX *ctx);

/*
 * Flash_CalcMd5 - Calculate MD5 hash of flash region
 */
void Flash_CalcMd5(const FLASH_CTX *ctx, DWORD addr, DWORD len, BYTE md5[16]);

#endif
