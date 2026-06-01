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

/* Flash commands (ESP8266/ESP32 ROM) */
#define ESP_CMD_FLASH_BEGIN     0x02
#define ESP_CMD_FLASH_DATA      0x03
#define ESP_CMD_FLASH_END       0x04

/* Memory operation commands */
#define ESP_CMD_MEM_BEGIN       0x05
#define ESP_CMD_MEM_END         0x06
#define ESP_CMD_MEM_DATA        0x07

/* Synchronization command */
#define ESP_CMD_SYNC            0x08

/* Register access commands */
#define ESP_CMD_WRITE_REG       0x09
#define ESP_CMD_READ_REG        0x0A

/* Baud rate change */
#define ESP_CMD_CHANGE_BAUDRATE 0x0F

/* Compressed flash write commands */
#define ESP_CMD_FLASH_DEFL_BEGIN  0x10
#define ESP_CMD_FLASH_DEFL_DATA   0x11
#define ESP_CMD_FLASH_DEFL_END    0x12

/* Flash MD5 */
#define ESP_CMD_SPI_FLASH_MD5   0x13

/* Security info (ESP32-S2+) */
#define ESP_CMD_GET_SECURITY_INFO 0x14

/* Stub-only commands */
#define ESP_CMD_ERASE_FLASH     0xD0
#define ESP_CMD_ERASE_REGION    0xD1
#define ESP_CMD_READ_FLASH      0xD2

/* SYNC sequence length */
#define ESP_SYNC_SEQ_LEN    36

/* Flash block size for erase operations */
#define ESP_FLASH_BLOCK_SIZE    0x1000
#define ESP_FLASH_ERASE_SIZE    0x10000

/* Callback type for device modification notification */
typedef void (*ESP_MODIFIED_CB)(void);

/* Callback type for writing data to serial port */
typedef DWORD (*ESP_WRITE_CB)(const BYTE *data, DWORD len);

/* Callback type for changing baud rate */
typedef BOOL (*ESP_BAUDRATE_CB)(DWORD baudRate);

/* ESP protocol context */
typedef struct {
    SLIP_CTX  slip;           /* SLIP decoder context */
    CHIP_CTX  chip;           /* Chip characteristics */
    FLASH_CTX flash;          /* Flash storage */
    BOOL      synced;         /* SYNC handshake completed */
    HWND      hNotify;        /* Window for UI notifications */
    ESP_MODIFIED_CB onModified; /* Device modification callback */
    ESP_WRITE_CB onWrite;     /* Serial write callback */
    ESP_BAUDRATE_CB onBaudRate; /* Baud rate change callback */
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

/* Set write callback for sending data to serial port */
void Esptool_SetWriteCallback(ESPTOOL_CTX *ctx, ESP_WRITE_CB cb);

/* Set baud rate change callback */
void Esptool_SetBaudRateCallback(ESPTOOL_CTX *ctx, ESP_BAUDRATE_CB cb);

/* Feed raw serial data to protocol decoder */
BOOL Esptool_Feed(ESPTOOL_CTX *ctx, const BYTE *data, int len);

/* Process a complete SLIP frame */
BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len);

/* Send response packet */
void Esptool_SendResponse(ESPTOOL_CTX *ctx, BYTE cmd, DWORD status, const BYTE *data, WORD data_len);

/* Calculate XOR checksum */
BYTE Esptool_CalcChecksum(const BYTE *data, int len);

#endif
