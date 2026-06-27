/*
 * chip_priv.h - Internal chip register constants and cross-module declarations
 *
 * Not part of the public API. Used only by chip.c and efuse.c.
 */

#ifndef FESP_CHIP_PRIV_H
#define FESP_CHIP_PRIV_H

#include "fesp.h"

/* ============================================================================
 * eFuse base addresses per chip type
 * ============================================================================
 */

#define FESP_EFUSE_RD_REG_BASE_ESP8266 0x3FF00050

#define FESP_EFUSE_BASE_ESP32 0x3FF00000
#define FESP_EFUSE_RD_REG_BASE_ESP32 0x3FF5A000

#define FESP_EFUSE_BASE_ESP32S2 0x3F41A000
#define FESP_EFUSE_BASE_ESP32S3 0x60007000
#define FESP_EFUSE_BASE_ESP32C2 0x60008800
#define FESP_EFUSE_BASE_ESP32C3 0x60008800
#define FESP_EFUSE_BASE_ESP32C6 0x600B0800

/* ============================================================================
 * MAC eFuse offsets per chip type
 * ============================================================================
 */

#define FESP_MAC_EFUSE_WORD0_ESP8266 (FESP_EFUSE_RD_REG_BASE_ESP8266 + 0x00)
#define FESP_MAC_EFUSE_WORD1_ESP8266 (FESP_EFUSE_RD_REG_BASE_ESP8266 + 0x04)
#define FESP_MAC_EFUSE_WORD3_ESP8266 (FESP_EFUSE_RD_REG_BASE_ESP8266 + 0x0C)

#define FESP_MAC_EFUSE_WORD1_ESP32 (FESP_EFUSE_RD_REG_BASE_ESP32 + 0x04)
#define FESP_MAC_EFUSE_WORD2_ESP32 (FESP_EFUSE_RD_REG_BASE_ESP32 + 0x08)

#define FESP_MAC_EFUSE_BASE_ESP32S2 (FESP_EFUSE_BASE_ESP32S2 + 0x044)
#define FESP_MAC_EFUSE_BASE_ESP32S3 (FESP_EFUSE_BASE_ESP32S3 + 0x044)
#define FESP_MAC_EFUSE_BASE_ESP32C2 (FESP_EFUSE_BASE_ESP32C2 + 0x040)
#define FESP_MAC_EFUSE_BASE_ESP32C3 (FESP_EFUSE_BASE_ESP32C3 + 0x044)
#define FESP_MAC_EFUSE_BASE_ESP32C6 (FESP_EFUSE_BASE_ESP32C6 + 0x044)

/* ============================================================================
 * SPI register base addresses per chip type
 * ============================================================================
 */

#define FESP_SPI_REG_BASE_ESP8266 0x60000200
#define FESP_SPI_REG_BASE_ESP32 0x3FF42000
#define FESP_SPI_REG_BASE_ESP32S2 0x3F402000
#define FESP_SPI_REG_BASE_ESP32S3 0x60002000
#define FESP_SPI_REG_BASE_ESP32C6 0x60003000

/* ============================================================================
 * UART register addresses per chip type
 * ============================================================================
 */

#define FESP_UART_CLKDIV_REG_ESP32 0x3FF40014
#define FESP_UART_CLKDIV_REG_ESP32S2 0x3F400014
#define FESP_UART_CLKDIV_REG_ESP8266 0x60000014
#define FESP_UART_CLKDIV_MASK 0xFFFFF

/* ============================================================================
 * Flash size register
 * ============================================================================
 */

#define FESP_FLASH_SIZE_REG_ESP32 0x3F400010

/* ============================================================================
 * SPI register offsets and bit definitions
 * ============================================================================
 */

#define FESP_SPI_CMD_OFFS 0x00
#define FESP_SPI_ADDR_OFFS 0x04

#define FESP_SPI_CMD_USR (1 << 18)
#define FESP_SPI_USR_COMMAND (1 << 31)
#define FESP_SPI_USR_ADDR (1 << 30)
#define FESP_SPI_USR_DUMMY (1 << 29)
#define FESP_SPI_USR_MISO (1 << 28)
#define FESP_SPI_USR_MOSI (1 << 27)

/* ============================================================================
 * SPI flash commands
 * ============================================================================
 */

#define FESP_SPIFLASH_RDID 0x9F

/* ============================================================================
 * Chip ID values and IMAGE_CHIP_ID values
 * ============================================================================
 */

#define FESP_CHIP_ID_ESP8266 0xFFF0C101
#define FESP_CHIP_ID_ESP32 0x00F01D83
#define FESP_CHIP_ID_ESP32S2 0x000007C6
#define FESP_CHIP_ID_ESP32S3 0x00000009
#define FESP_CHIP_ID_ESP32C2 0x7C41A06F
#define FESP_CHIP_ID_ESP32C3 0x6921506F
#define FESP_CHIP_ID_ESP32C6 0x2CE0806F

#define IMAGE_FESP_CHIP_ID_ESP8266 0
#define IMAGE_FESP_CHIP_ID_ESP32 0
#define IMAGE_FESP_CHIP_ID_ESP32S2 2
#define IMAGE_FESP_CHIP_ID_ESP32S3 9
#define FESP_IMAGE_FESP_CHIP_ID_ESP32C2 12
#define IMAGE_FESP_CHIP_ID_ESP32C3 5
#define IMAGE_FESP_CHIP_ID_ESP32C6 13

/* ============================================================================
 * Cross-module internal function declarations (defined in efuse.c)
 * ============================================================================
 */

bool fesp_chip_write_reg_esp32(fesp_chip_ctx_t *ctx, int offset, uint32_t val);
bool fesp_chip_write_reg_modern(fesp_chip_ctx_t *ctx, int offset, uint32_t val);

#endif /* FESP_CHIP_PRIV_H */
