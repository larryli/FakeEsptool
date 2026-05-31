#ifndef ESP_DEVICE_H
#define ESP_DEVICE_H

#include <windows.h>
#include "chip.h"
#include "flash.h"

#define DEVICE_MAGIC    0x45535000
#define DEVICE_VERSION  1

typedef struct {
    CHIP_CTX  chip;
    FLASH_CTX flash;
    WCHAR     filename[MAX_PATH];
    BOOL      modified;
} DEVICE_CTX;

BOOL Device_Init(DEVICE_CTX *ctx, CHIP_TYPE chipType, DWORD flashSize, const BYTE mac[6]);
void Device_Close(DEVICE_CTX *ctx);
BOOL Device_Save(DEVICE_CTX *ctx, const WCHAR *filename);
BOOL Device_Load(DEVICE_CTX *ctx, const WCHAR *filename);
BOOL Device_IsModified(const DEVICE_CTX *ctx);
void Device_SetModified(DEVICE_CTX *ctx, BOOL modified);
const WCHAR *Device_GetFilename(const DEVICE_CTX *ctx);

#endif
