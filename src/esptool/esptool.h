/*
 * esptool.h - esptool protocol handler
 *
 * Parses SLIP frames, routes commands, and sends responses.
 */

#ifndef ESP_ESPTOOL_H
#define ESP_ESPTOOL_H

#include <windows.h>
#include "slip.h"
#include "chip.h"
#include "flash.h"

#ifndef WM_SERIAL_TX
#define WM_SERIAL_TX (WM_USER + 2)
#endif

/* Packet direction */
#define ESP_DIR_REQUEST     0x00
#define ESP_DIR_RESPONSE    0x01

/* Response status codes */
#define ESP_OK              0x00
#define ESP_FAIL            0x01

/* Memory operation commands */
#define ESP_CMD_MEM_BEGIN       0x05
#define ESP_CMD_MEM_END         0x06
#define ESP_CMD_MEM_DATA        0x07

/* Synchronization command */
#define ESP_CMD_SYNC            0x08

/* SPI and flash commands */
#define ESP_CMD_SPI_SET_PARAMS  0x09
#define ESP_CMD_SPI_ATTACH      0x0D
#define ESP_CMD_CHANGE_BAUDRATE 0x0F

/* Register access commands */
#define ESP_CMD_READ_REG        0x0A
#define ESP_CMD_WRITE_REG       0x0B

/* Flash operation commands */
#define ESP_CMD_SPI_FLASH_MD5   0x13
#define ESP_CMD_SPI_FLASH_DATA  0x14
#define ESP_CMD_SPI_READ_FLASH  0x15
#define ESP_CMD_SPI_ERASE_FLASH 0x16
#define ESP_CMD_SPI_ERASE_BLOCK 0x17
#define ESP_CMD_SPI_FLASH_BLOCK 0x18
#define ESP_CMD_READ_FLASH_SFDP 0x1A

/* Compressed flash write commands */
#define ESP_CMD_SPI_FLASH_DEFL_BEGIN  0x20
#define ESP_CMD_SPI_FLASH_DEFL_DATA   0x21
#define ESP_CMD_SPI_FLASH_DEFL_END    0x22
#define ESP_CMD_SPI_FLASH_DEFL_MD5    0x23

/* Legacy flash commands (unused) */
#define ESP_CMD_READ_FLASH      0xE0
#define ESP_CMD_WRITE_FLASH     0xE1

/* SYNC sequence length */
#define ESP_SYNC_SEQ_LEN    36

/* Flash block size for erase operations */
#define ESP_FLASH_BLOCK_SIZE    0x1000
#define ESP_FLASH_ERASE_SIZE    0x10000

/* Callback type for device modification notification */
typedef void (*ESP_MODIFIED_CB)(void);

/* ESP protocol context */
typedef struct {
    SLIP_CTX  slip;           /* SLIP decoder context */
    CHIP_CTX  chip;           /* Chip characteristics */
    FLASH_CTX flash;          /* Flash storage */
    BOOL      synced;         /* SYNC handshake completed */
    HWND      hNotify;        /* Window for UI notifications */
    ESP_MODIFIED_CB onModified; /* Device modification callback */
} ESPTOOL_CTX;

/* ESP protocol packet */
typedef struct {
    BYTE  direction;          /* Request (0x00) or Response (0x01) */
    BYTE  command;            /* Command code */
    WORD  size;               /* Data payload size */
    DWORD value;              /* Command-specific value */
    BYTE  data[2048];         /* Data payload */
} ESP_PACKET;

/* Initialize ESP protocol context */
void Esptool_Init(ESPTOOL_CTX *ctx);

/* Set notification window for TX data */
void Esptool_SetNotify(ESPTOOL_CTX *ctx, HWND hNotify);

/* Set callback for device modification */
void Esptool_SetModifiedCallback(ESPTOOL_CTX *ctx, ESP_MODIFIED_CB cb);

/* Set chip type and reinitialize */
void Esptool_SetChipType(ESPTOOL_CTX *ctx, CHIP_TYPE type);

/* Set flash size and reinitialize */
void Esptool_SetFlashSize(ESPTOOL_CTX *ctx, DWORD size);

/* Feed raw serial data to protocol decoder */
BOOL Esptool_Feed(ESPTOOL_CTX *ctx, const BYTE *data, int len);

/* Process a complete SLIP frame */
BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len);

/* Send response packet */
void Esptool_SendResponse(ESPTOOL_CTX *ctx, BYTE cmd, DWORD status, const BYTE *data, WORD data_len);

/* Calculate XOR checksum */
BYTE Esptool_CalcChecksum(const BYTE *data, int len);

#endif
