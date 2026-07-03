/*
 * device_file.h - .esp device file format management
 *
 * Handles save/load of device configuration files.
 * This is GUI-layer code, not part of the esptool simulation engine.
 */

#ifndef DEVICE_FILE_H
#define DEVICE_FILE_H

#include "fesptool/chip.h"
#include "fesptool/flash.h"
#include <windows.h>

/* Device file format constants */
#define DEVICE_FILE_MAGIC 0x45535000 /* "ESP\0" */
#define DEVICE_FILE_VERSION 2

/*
 * DeviceFile_GetEfuseBlockSize - Get eFuse block data size for QEMU format
 *
 * Returns the size of eFuse block read data (ROM portion) for the given chip.
 * ESP8266 returns 0 (no block structure).
 *
 * @type: Chip type enum
 *
 * Returns block data size in bytes, or 0 for ESP8266.
 */
int DeviceFile_GetEfuseBlockSize(fesp_chip_type_t type);

/*
 * DeviceFile_ExportEfuseBlocks - Export eFuse block data to buffer
 *
 * Extracts packed block data from efuse array into output buffer.
 * Used by Export eFuse menu and CLI --export-efuse.
 *
 * @chip:     Pointer to chip context
 * @out:      Output buffer (must be >= DeviceFile_GetEfuseBlockSize bytes)
 * @outSize:  Size of output buffer
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL DeviceFile_ExportEfuseBlocks(const fesp_chip_ctx_t *chip, uint8_t *out,
                                  int outSize);

/*
 * DeviceFile_ImportEfuseBlocks - Import eFuse block data from buffer
 *
 * Writes packed block data from buffer into efuse array.
 * Used by Import eFuse menu and CLI --import-efuse.
 *
 * @chip:     Pointer to chip context
 * @data:     Input buffer with packed block data
 * @dataSize: Size of input buffer
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL DeviceFile_ImportEfuseBlocks(fesp_chip_ctx_t *chip, const uint8_t *data,
                                  int dataSize);

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
BOOL DeviceFile_Save(fesp_chip_ctx_t *chip, fesp_flash_ctx_t *flash,
                     const WCHAR *filename);

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
BOOL DeviceFile_Load(fesp_chip_ctx_t *chip, fesp_flash_ctx_t *flash,
                     const WCHAR *filename);

#endif /* DEVICE_FILE_H */
