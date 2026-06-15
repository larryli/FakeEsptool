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

/* WM_SERIAL_TX - Serial data transmitted notification
 * Primary definition in main.h. This is a fallback definition.
 * wParam: bytes sent, lParam: pointer to sent data (HeapAlloc'd, receiver frees) */
#ifndef WM_SERIAL_TX
#define WM_SERIAL_TX (WM_USER + 2)
#endif

/* Packet direction */
#define ESP_DIR_REQUEST     0x00
#define ESP_DIR_RESPONSE    0x01

/* Response status codes */
#define ESP_OK              0x00
#define ESP_FAIL            0x01

/* Protocol state machine */
typedef enum {
    ESP_STATE_IDLE,            /* Initial state, waiting for SYNC */
    ESP_STATE_SYNCED,          /* SYNC received, waiting for chip detection */
    ESP_STATE_READY,           /* Chip detected, ready for commands */
    ESP_STATE_FLASH_WRITING,   /* FLASH_BEGIN received, waiting for data */
    ESP_STATE_MEM_WRITING,     /* MEM_BEGIN received, waiting for data */
} ESP_STATE;

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

/* SPI flash parameters (ESP32+ ROM, all stubs) */
#define ESP_CMD_SPI_SET_PARAMS  0x0B

/* SPI flash attach */
#define ESP_CMD_SPI_ATTACH      0x0D

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
#define ESP_CMD_RUN_USER_CODE   0xD3

/* NAND flash commands (stub-only, ESP32-S3 etc.) */
#define ESP_CMD_SPI_NAND_ATTACH           0xD5
#define ESP_CMD_SPI_NAND_READ_SPARE       0xD6
#define ESP_CMD_SPI_NAND_WRITE_SPARE      0xD7
#define ESP_CMD_SPI_NAND_READ_FLASH       0xD8
#define ESP_CMD_SPI_NAND_WRITE_FLASH_BEGIN 0xD9
#define ESP_CMD_SPI_NAND_WRITE_FLASH_DATA  0xDA
#define ESP_CMD_SPI_NAND_ERASE_FLASH      0xDB
#define ESP_CMD_SPI_NAND_ERASE_REGION     0xDC
#define ESP_CMD_SPI_NAND_READ_PAGE_DEBUG  0xDD
#define ESP_CMD_SPI_NAND_WRITE_FLASH_END  0xDE

/* SYNC response count (real device sends 8 identical responses) */
#define ESP_SYNC_RESPONSE_COUNT 8

/* Maximum data payload size in ESP_PACKET (SLIP_MAX_FRAME minus 8-byte packet header: dir+cmd+size+value) */
#define ESP_PACKET_DATA_MAX (SLIP_MAX_FRAME - 8)

/* Status length: 2 bytes for stub mode, 4 bytes for ROM mode */
#define ESP_STATUS_LEN(ctx) ((ctx)->stub_mode ? 2 : 4)

/* Flash block size for erase operations */
#define ESP_FLASH_BLOCK_SIZE    0x1000
#define ESP_FLASH_ERASE_SIZE    0x10000

/* Callback type for device modification notification */
typedef void (*ESP_MODIFIED_CB)(void);

/* Callback type for writing data to serial port */
typedef DWORD (*ESP_WRITE_CB)(const BYTE *data, DWORD len);

/* Callback type for changing baud rate */
typedef BOOL (*ESP_BAUDRATE_CB)(DWORD baudRate);

/* ESP protocol packet */
typedef struct {
    BYTE  direction;          /* Request (0x00) or Response (0x01) */
    BYTE  command;            /* Command code */
    WORD  size;               /* Data payload size */
    DWORD value;              /* Command-specific value */
    BYTE  data[ESP_PACKET_DATA_MAX]; /* Data payload */
} ESP_PACKET;

/* Forward declaration */
typedef struct DEVICE_CTX_TAG DEVICE_CTX;

/* ESP protocol context */
typedef struct {
    SLIP_CTX  slip;           /* SLIP decoder context */
    CHIP_CTX  *chip;          /* Pointer to device chip (not owned) */
    FLASH_CTX *flash;         /* Pointer to device flash (not owned) */
    ESP_PACKET pkt;           /* Pre-allocated packet buffer (avoids stack overflow) */
    ESP_STATE state;          /* Protocol state machine */
    BOOL      synced;         /* SYNC handshake completed */
    BOOL      stub_mode;      /* Stub is running (OHAI received) */
    HWND      hNotify;        /* Window for UI notifications */
    ESP_MODIFIED_CB onModified; /* Device modification callback */
    ESP_WRITE_CB onWrite;     /* Serial write callback */
    ESP_BAUDRATE_CB onBaudRate; /* Baud rate change callback */
    DWORD     flash_offset;   /* Current flash write offset */
    DWORD     flash_seq;      /* Current flash write sequence */
    DWORD     last_read_val;  /* Cached value from last READ_REG */
    DWORD     flash_uncompressed_size; /* Uncompressed size for DEFLATE */
    BYTE      *defl_buf;      /* Compressed data accumulation buffer */
    DWORD     defl_buf_size;  /* Current accumulated data size */
    DWORD     defl_buf_cap;   /* Buffer capacity (flash_uncompressed_size) */
    DWORD     defl_offset;    /* Flash offset for current deflate session */
    DWORD     defl_unc_size;  /* Uncompressed size for current deflate session */
    BOOL      flash_encrypted; /* Current flash session uses encryption (encrypted=1) */
} ESPTOOL_CTX;

/*
 * Esptool_Init - Initialize ESP protocol context with device data
 */
void Esptool_Init(ESPTOOL_CTX *ctx, CHIP_CTX *chip, FLASH_CTX *flash);

/*
 * Esptool_ResetState - Reset protocol state (called on download mode entry)
 */
void Esptool_ResetState(ESPTOOL_CTX *ctx);

/*
 * Esptool_SetNotify - Set notification window for TX data
 */
void Esptool_SetNotify(ESPTOOL_CTX *ctx, HWND hNotify);

/*
 * Esptool_SetModifiedCallback - Set callback for device modification
 */
void Esptool_SetModifiedCallback(ESPTOOL_CTX *ctx, ESP_MODIFIED_CB cb);

/*
 * Esptool_SetWriteCallback - Set write callback for sending data to serial port
 */
void Esptool_SetWriteCallback(ESPTOOL_CTX *ctx, ESP_WRITE_CB cb);

/*
 * Esptool_SetBaudRateCallback - Set baud rate change callback
 */
void Esptool_SetBaudRateCallback(ESPTOOL_CTX *ctx, ESP_BAUDRATE_CB cb);

/*
 * Esptool_Feed - Feed raw serial data to protocol decoder
 */
BOOL Esptool_Feed(ESPTOOL_CTX *ctx, const BYTE *data, int len);

/*
 * Esptool_ProcessFrame - Process a complete SLIP frame
 */
BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len);

/*
 * Esptool_SendResponse - Send response packet with status in data
 */
void Esptool_SendResponse(ESPTOOL_CTX *ctx, BYTE cmd, DWORD req_val, DWORD status, const BYTE *data, WORD data_len);

/*
 * Esptool_SendResponseEx - Send response packet with configurable status length
 *
 * @ctx:        Protocol context
 * @cmd:        Command code (echoed in response)
 * @req_val:    Value field (usually last READ_REG value for non-SYNC/READ_REG)
 * @status:     ESP_OK (0x00) or ESP_FAIL (0x01)
 * @status_len: Status bytes (2 for stub mode, 4 for ROM mode)
 * @data:       Optional data payload (can be NULL if data_len=0)
 * @data_len:   Data payload length in bytes
 */
void Esptool_SendResponseEx(ESPTOOL_CTX *ctx, BYTE cmd, DWORD req_val, DWORD status, BYTE status_len, const BYTE *data, WORD data_len);

/*
 * Esptool_CalcChecksum - Calculate XOR checksum
 */
BYTE Esptool_CalcChecksum(const BYTE *data, int len);

#endif
