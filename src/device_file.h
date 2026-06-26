/*
 * device_file.h - .esp device file format management
 *
 * Handles save/load of device configuration files.
 * This is GUI-layer code, not part of the esptool simulation engine.
 */

#ifndef DEVICE_FILE_H
#define DEVICE_FILE_H

#include "esptool/chip.h"
#include "esptool/flash.h"
#include <windows.h>

/* Device file format constants */
#define DEVICE_FILE_MAGIC 0x45535000   /* "ESP\0" */
#define DEVICE_FILE_VERSION 1

/*
 * DeviceFile_Save - Save device state to .esp file
 *
 * Writes chip config, eFuse data, and flash data to binary file.
 *
 * @chip:     Pointer to chip context
 * @flash:    Pointer to flash context
 * @filename: Path to save file
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL DeviceFile_Save(CHIP_CTX *chip, FLASH_CTX *flash, const WCHAR *filename);

/*
 * DeviceFile_Load - Load device state from .esp file
 *
 * Reads chip config, eFuse data, and flash data from binary file.
 * Calls Chip_Close/Flash_Close internally before loading new data.
 *
 * @chip:     Pointer to chip context (will be re-initialized)
 * @flash:    Pointer to flash context (will be re-initialized)
 * @filename: Path to load file
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL DeviceFile_Load(CHIP_CTX *chip, FLASH_CTX *flash, const WCHAR *filename);

#endif /* DEVICE_FILE_H */
