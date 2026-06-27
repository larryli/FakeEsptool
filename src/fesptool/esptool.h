/*
 * esptool.h - esptool protocol handler (public API)
 */

#ifndef FESP_ESPTOOL_H
#define FESP_ESPTOOL_H

#include "chip.h"
#include "efuse.h"
#include "flash.h"
#include "slip.h"

#define FESP_DIR_REQUEST 0x00
#define FESP_DIR_RESPONSE 0x01
#define FESP_OK 0x00
#define FESP_FAIL 0x01

/* Flash commands (ESP8266/ESP32 ROM) */
#define FESP_CMD_FLASH_BEGIN 0x02
#define FESP_CMD_FLASH_DATA 0x03
#define FESP_CMD_FLASH_END 0x04

/* Memory operation commands */
#define FESP_CMD_MEM_BEGIN 0x05
#define FESP_CMD_MEM_END 0x06
#define FESP_CMD_MEM_DATA 0x07

/* Synchronization command */
#define FESP_CMD_SYNC 0x08

/* Register access commands */
#define FESP_CMD_WRITE_REG 0x09
#define FESP_CMD_READ_REG 0x0A

/* SPI flash parameters (ESP32+ ROM, all stubs) */
#define FESP_CMD_SPI_SET_PARAMS 0x0B
#define FESP_CMD_SPI_ATTACH 0x0D
#define FESP_CMD_CHANGE_BAUDRATE 0x0F

/* Compressed flash write commands */
#define FESP_CMD_FLASH_DEFL_BEGIN 0x10
#define FESP_CMD_FLASH_DEFL_DATA 0x11
#define FESP_CMD_FLASH_DEFL_END 0x12

/* Flash MD5 */
#define FESP_CMD_SPI_FLASH_MD5 0x13

/* Security info (ESP32-S2+) */
#define FESP_CMD_GET_SECURITY_INFO 0x14

/* Stub-only commands */
#define FESP_CMD_ERASE_FLASH 0xD0
#define FESP_CMD_ERASE_REGION 0xD1
#define FESP_CMD_READ_FLASH 0xD2
#define FESP_CMD_RUN_USER_CODE 0xD3

/* NAND flash commands (stub-only) */
#define FESP_CMD_SPI_NAND_ATTACH 0xD5
#define FESP_CMD_SPI_NAND_READ_SPARE 0xD6
#define FESP_CMD_SPI_NAND_WRITE_SPARE 0xD7
#define FESP_CMD_SPI_NAND_READ_FLASH 0xD8
#define FESP_CMD_SPI_NAND_WRITE_FLASH_BEGIN 0xD9
#define FESP_CMD_SPI_NAND_WRITE_FLASH_DATA 0xDA
#define FESP_CMD_SPI_NAND_ERASE_FLASH 0xDB
#define FESP_CMD_SPI_NAND_ERASE_REGION 0xDC
#define FESP_CMD_SPI_NAND_READ_PAGE_DEBUG 0xDD
#define FESP_CMD_SPI_NAND_WRITE_FLASH_END 0xDE

#define FESP_SYNC_RESPONSE_COUNT 8

#define fesp_packet_t_DATA_MAX (FESP_SLIP_MAX_FRAME - 8)
#define FESP_STATUS_LEN(ctx) ((ctx)->stub_mode ? 2 : 4)

#define FESP_FLASH_BLOCK_SIZE 0x1000
#define FESP_FLASH_ERASE_SIZE 0x10000

typedef enum {
    FESP_STATE_IDLE,
    FESP_STATE_SYNCED,
    FESP_STATE_READY,
    FESP_STATE_FLASH_WRITING,
    FESP_STATE_MEM_WRITING,
} fesp_state_t;

typedef struct {
    uint8_t direction;
    uint8_t command;
    uint16_t size;
    uint32_t value;
    uint8_t data[fesp_packet_t_DATA_MAX];
} fesp_packet_t;

typedef struct {
    fesp_slip_ctx_t slip;
    fesp_chip_ctx_t *chip;
    fesp_flash_ctx_t *flash;
    fesp_packet_t pkt;
    fesp_state_t state;
    bool synced;
    bool stub_mode;
    uint32_t flash_offset;
    uint32_t flash_seq;
    uint32_t last_read_val;
    uint32_t flash_uncompressed_size;
    uint8_t *defl_buf;
    uint32_t defl_buf_size;
    uint32_t defl_buf_cap;
    uint32_t defl_offset;
    uint32_t defl_unc_size;
    bool flash_encrypted;
    uint8_t *decomp_buf;
    uint32_t decomp_buf_cap;
} fesp_ctx_t;

void fesp_init(fesp_ctx_t *ctx, fesp_chip_ctx_t *chip, fesp_flash_ctx_t *flash);
void fesp_close(fesp_ctx_t *ctx);
void fesp_reset_state(fesp_ctx_t *ctx);
bool fesp_feed(fesp_ctx_t *ctx, const uint8_t *data, int len);
bool fesp_process_frame(fesp_ctx_t *ctx, const uint8_t *frame, int frame_len);
void fesp_send_response(fesp_ctx_t *ctx, uint8_t cmd, uint32_t req_val,
                        uint32_t status, const uint8_t *data,
                        uint16_t data_len);
void fesp_send_response_ex(fesp_ctx_t *ctx, uint8_t cmd, uint32_t req_val,
                           uint32_t status, uint8_t status_len,
                           const uint8_t *data, uint16_t data_len);
uint8_t fesp_calc_checksum(const uint8_t *data, int len);

#endif /* FESP_ESPTOOL_H */
