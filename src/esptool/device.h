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
#define DEVICE_VERSION  1

/*
 * Device file format v1 layout:
 *
 * [Header] 16 bytes
 *   0x00  magic        4B   Magic number (0x45535000)
 *   0x04  version      4B   Format version (1)
 *   0x08  chipType     4B   Chip type enum
 *   0x0C  xtalFreq     1B   Crystal frequency enum
 *   0x0D  reserved     3B   Reserved (zero fill)
 *
 * [MAC] 8 bytes
 *   0x10  mac          6B   MAC address
 *   0x16  reserved     2B   Reserved (zero fill)
 *
 * [Flash Config] 4 bytes
 *   0x18  flashSize    4B   Flash size in bytes
 *
 * [eFuse] variable
 *   0x1C  efuseSize    4B   eFuse data size
 *   0x20  efuse        N    eFuse data
 *
 * [Flash Data] variable
 *         flash        M    Flash data (flashSize bytes)
 */

/* Device context */
typedef struct DEVICE_CTX_TAG {
    CHIP_CTX  chip;             /* Chip characteristics */
    FLASH_CTX flash;            /* Flash storage */
    WCHAR     filename[MAX_PATH]; /* Current file path */
    BOOL      modified;         /* TRUE if data has been modified */
} DEVICE_CTX;

/*
 * Device_Init - Initialize new device with chip type, flash size, and MAC
 */
BOOL Device_Init(DEVICE_CTX *ctx, CHIP_TYPE chipType, DWORD flashSize, const BYTE mac[6]);

/*
 * Device_Close - Release device resources
 */
void Device_Close(DEVICE_CTX *ctx);

/*
 * Device_Save - Save device to .esp file
 */
BOOL Device_Save(DEVICE_CTX *ctx, const WCHAR *filename);

/*
 * Device_Load - Load device from .esp file
 */
BOOL Device_Load(DEVICE_CTX *ctx, const WCHAR *filename);

/*
 * Device_IsModified - Check if device data has been modified
 */
BOOL Device_IsModified(const DEVICE_CTX *ctx);

/*
 * Device_SetModified - Set or clear modification flag
 */
void Device_SetModified(DEVICE_CTX *ctx, BOOL modified);

/*
 * Device_GetFilename - Get current file path
 */
const WCHAR *Device_GetFilename(const DEVICE_CTX *ctx);

#endif
