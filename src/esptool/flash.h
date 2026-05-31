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

/* Flash storage context */
typedef struct {
    BYTE *data;             /* Flash data buffer */
    DWORD size;             /* Flash size in bytes */
    BOOL  allocated;        /* TRUE if data buffer was allocated */
} FLASH_CTX;

/* Initialize flash with specified size */
BOOL Flash_Init(FLASH_CTX *ctx, DWORD size);

/* Release flash resources */
void Flash_Close(FLASH_CTX *ctx);

/* Read data from flash */
BOOL Flash_Read(const FLASH_CTX *ctx, DWORD addr, BYTE *buf, DWORD len);

/* Write data to flash (AND operation) */
BOOL Flash_Write(FLASH_CTX *ctx, DWORD addr, const BYTE *data, DWORD len);

/* Erase flash region (set to 0xFF) */
BOOL Flash_Erase(FLASH_CTX *ctx, DWORD addr, DWORD len);

/* Erase entire flash */
BOOL Flash_EraseAll(FLASH_CTX *ctx);

/* Calculate MD5 hash of flash region */
void Flash_CalcMd5(const FLASH_CTX *ctx, DWORD addr, DWORD len, BYTE md5[16]);

#endif
