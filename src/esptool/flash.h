#ifndef ESP_FLASH_H
#define ESP_FLASH_H

#include <windows.h>

#define FLASH_ERASE_PATTERN 0xFF

typedef struct {
    BYTE *data;
    DWORD size;
    BOOL  allocated;
} FLASH_CTX;

BOOL Flash_Init(FLASH_CTX *ctx, DWORD size);
void Flash_Close(FLASH_CTX *ctx);
BOOL Flash_Read(const FLASH_CTX *ctx, DWORD addr, BYTE *buf, DWORD len);
BOOL Flash_Write(FLASH_CTX *ctx, DWORD addr, const BYTE *data, DWORD len);
BOOL Flash_Erase(FLASH_CTX *ctx, DWORD addr, DWORD len);
BOOL Flash_EraseAll(FLASH_CTX *ctx);
void Flash_CalcMd5(const FLASH_CTX *ctx, DWORD addr, DWORD len, BYTE md5[16]);

#endif
