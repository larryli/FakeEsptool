/*
 * esptool.h - esptool protocol handler
 *
 * Parses SLIP frames, routes commands, and sends responses.
 */

#ifndef ESP_ESPTOOL_H
#define ESP_ESPTOOL_H

#include "chip.h"
#include "flash.h"
#include "slip.h"

/* Packet direction */
#define ESP_DIR_REQUEST 0x00
#define ESP_DIR_RESPONSE 0x01

/* Response status codes */
#define ESP_OK 0x00
#define ESP_FAIL 0x01

/* Protocol state machine */
typedef enum {
    ESP_STATE_IDLE,          /* Initial state, waiting for SYNC */
    ESP_STATE_SYNCED,        /* SYNC received, waiting for chip detection */
    ESP_STATE_READY,         /* Chip detected, ready for commands */
    ESP_STATE_FLASH_WRITING, /* FLASH_BEGIN received, waiting for data */
    ESP_STATE_MEM_WRITING,   /* MEM_BEGIN received, waiting for data */
} ESP_STATE;

/* Flash commands (ESP8266/ESP32 ROM) */
#define ESP_CMD_FLASH_BEGIN 0x02
#define ESP_CMD_FLASH_DATA 0x03
#define ESP_CMD_FLASH_END 0x04

/* Memory operation commands */
#define ESP_CMD_MEM_BEGIN 0x05
#define ESP_CMD_MEM_END 0x06
#define ESP_CMD_MEM_DATA 0x07

/* Synchronization command */
#define ESP_CMD_SYNC 0x08

/* Register access commands */
#define ESP_CMD_WRITE_REG 0x09
#define ESP_CMD_READ_REG 0x0A

/* SPI flash parameters (ESP32+ ROM, all stubs) */
#define ESP_CMD_SPI_SET_PARAMS 0x0B

/* SPI flash attach */
#define ESP_CMD_SPI_ATTACH 0x0D

/* Baud rate change */
#define ESP_CMD_CHANGE_BAUDRATE 0x0F

/* Compressed flash write commands */
#define ESP_CMD_FLASH_DEFL_BEGIN 0x10
#define ESP_CMD_FLASH_DEFL_DATA 0x11
#define ESP_CMD_FLASH_DEFL_END 0x12

/* Flash MD5 */
#define ESP_CMD_SPI_FLASH_MD5 0x13

/* Security info (ESP32-S2+) */
#define ESP_CMD_GET_SECURITY_INFO 0x14

/* Stub-only commands */
#define ESP_CMD_ERASE_FLASH 0xD0
#define ESP_CMD_ERASE_REGION 0xD1
#define ESP_CMD_READ_FLASH 0xD2
#define ESP_CMD_RUN_USER_CODE 0xD3

/* NAND flash commands (stub-only, ESP32-S3 etc.) */
#define ESP_CMD_SPI_NAND_ATTACH 0xD5
#define ESP_CMD_SPI_NAND_READ_SPARE 0xD6
#define ESP_CMD_SPI_NAND_WRITE_SPARE 0xD7
#define ESP_CMD_SPI_NAND_READ_FLASH 0xD8
#define ESP_CMD_SPI_NAND_WRITE_FLASH_BEGIN 0xD9
#define ESP_CMD_SPI_NAND_WRITE_FLASH_DATA 0xDA
#define ESP_CMD_SPI_NAND_ERASE_FLASH 0xDB
#define ESP_CMD_SPI_NAND_ERASE_REGION 0xDC
#define ESP_CMD_SPI_NAND_READ_PAGE_DEBUG 0xDD
#define ESP_CMD_SPI_NAND_WRITE_FLASH_END 0xDE

/* SYNC response count (real device sends 8 identical responses) */
#define ESP_SYNC_RESPONSE_COUNT 8

/* Maximum data payload size in ESP_PACKET (SLIP_MAX_FRAME minus 8-uint8_t packet
 * header: dir+cmd+size+value) */
#define ESP_PACKET_DATA_MAX (SLIP_MAX_FRAME - 8)

/* Status length: 2 bytes for stub mode, 4 bytes for ROM mode */
#define ESP_STATUS_LEN(ctx) ((ctx)->stub_mode ? 2 : 4)

/* Flash block size for erase operations */
#define ESP_FLASH_BLOCK_SIZE 0x1000
#define ESP_FLASH_ERASE_SIZE 0x10000

/* ESP protocol packet */
typedef struct {
    uint8_t direction;                 /* Request (0x00) or Response (0x01) */
    uint8_t command;                   /* Command code */
    uint16_t size;                      /* Data payload size */
    uint32_t value;                    /* Command-specific value */
    uint8_t data[ESP_PACKET_DATA_MAX]; /* Data payload */
} ESP_PACKET;

/* ESP protocol context */
typedef struct {
    SLIP_CTX slip;    /* SLIP decoder context */
    CHIP_CTX *chip;   /* Pointer to device chip (not owned) */
    FLASH_CTX *flash; /* Pointer to device flash (not owned) */
    ESP_PACKET pkt;   /* Pre-allocated packet buffer (avoids stack overflow) */
    ESP_STATE state;  /* Protocol state machine */
    bool synced;      /* SYNC handshake completed */
    bool stub_mode;   /* Stub is running (OHAI received) */
    uint32_t flash_offset;            /* Current flash write offset */
    uint32_t flash_seq;               /* Current flash write sequence */
    uint32_t last_read_val;           /* Cached value from last READ_REG */
    uint32_t flash_uncompressed_size; /* Uncompressed size for DEFLATE */
    uint8_t *defl_buf;                /* Compressed data accumulation buffer */
    uint32_t defl_buf_size;           /* Current accumulated data size */
    uint32_t defl_buf_cap;   /* Buffer capacity (flash_uncompressed_size) */
    uint32_t defl_offset;    /* Flash offset for current deflate session */
    uint32_t defl_unc_size;  /* Uncompressed size for current deflate session */
    bool flash_encrypted; /* Current flash session uses encryption (encrypted=1)
                           */
    uint8_t *decomp_buf;     /* Persistent decompression buffer */
    uint32_t decomp_buf_cap; /* Decompression buffer capacity */
} ESPTOOL_CTX;

/*
 * Esptool_Init - Initialize ESP protocol context with device data
 */
void Esptool_Init(ESPTOOL_CTX *ctx, CHIP_CTX *chip, FLASH_CTX *flash);

/*
 * Esptool_Close - Release persistent resources (decompression buffer)
 */
void Esptool_Close(ESPTOOL_CTX *ctx);

/*
 * Esptool_ResetState - Reset protocol state (called on download mode entry)
 */
void Esptool_ResetState(ESPTOOL_CTX *ctx);

/*
 * Esptool_Feed - Feed raw serial data to protocol decoder
 */
bool Esptool_Feed(ESPTOOL_CTX *ctx, const uint8_t *data, int len);

/*
 * Esptool_ProcessFrame - Process a complete SLIP frame
 */
bool Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const uint8_t *frame, int frame_len);

/*
 * Esptool_SendResponse - Send response packet with status in data
 */
void Esptool_SendResponse(ESPTOOL_CTX *ctx, uint8_t cmd, uint32_t req_val,
                          uint32_t status, const uint8_t *data, uint16_t data_len);

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
void Esptool_SendResponseEx(ESPTOOL_CTX *ctx, uint8_t cmd, uint32_t req_val,
                            uint32_t status, uint8_t status_len, const uint8_t *data,
                            uint16_t data_len);

/*
 * Esptool_CalcChecksum - Calculate XOR checksum
 */
uint8_t Esptool_CalcChecksum(const uint8_t *data, int len);

#endif
