/*
 * device.h - Device file management
 *
 * Handles save/load of device configuration (.esp files).
 */

#ifndef ESP_DEVICE_H
#define ESP_DEVICE_H

#include <windows.h>
#include "chip.h"
#include "flash.h"

/* Device file magic number ("ESP\0") */
#define DEVICE_MAGIC    0x45535000

/* Device file format version */
#define DEVICE_VERSION  2

/* Device context */
typedef struct {
    CHIP_CTX  chip;             /* Chip characteristics */
    FLASH_CTX flash;            /* Flash storage */
    WCHAR     filename[MAX_PATH]; /* Current file path */
    BOOL      modified;         /* TRUE if data has been modified */
} DEVICE_CTX;

/* Initialize new device with chip type, flash size, and MAC */
BOOL Device_Init(DEVICE_CTX *ctx, CHIP_TYPE chipType, DWORD flashSize, const BYTE mac[6]);

/* Release device resources */
void Device_Close(DEVICE_CTX *ctx);

/* Save device to .esp file */
BOOL Device_Save(DEVICE_CTX *ctx, const WCHAR *filename);

/* Load device from .esp file */
BOOL Device_Load(DEVICE_CTX *ctx, const WCHAR *filename);

/* Check if device data has been modified */
BOOL Device_IsModified(const DEVICE_CTX *ctx);

/* Set or clear modification flag */
void Device_SetModified(DEVICE_CTX *ctx, BOOL modified);

/* Get current file path */
const WCHAR *Device_GetFilename(const DEVICE_CTX *ctx);

#endif
