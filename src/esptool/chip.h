/*
 * chip.h - ESP chip characteristics
 *
 * Simulates chip properties, eFuse, and register access.
 */

#ifndef ESP_CHIP_H
#define ESP_CHIP_H

#include <windows.h>

/* Maximum chip name length */
#define CHIP_NAME_MAX 32

/* ============================================================================
 * Chip detection register (used by esptool for autodetect)
 * ============================================================================
 */
#define CHIP_DETECT_REG 0x40001000

/* ============================================================================
 * eFuse base addresses per chip type
 * ============================================================================
 */

/* ESP8266 eFuse */
#define EFUSE_RD_REG_BASE_ESP8266 0x3FF00050

/* ESP32 eFuse */
#define EFUSE_BASE_ESP32 0x3FF00000        /* Direct access */
#define EFUSE_RD_REG_BASE_ESP32 0x3FF5A000 /* esptool readEfuse */

/* ESP32-S2 eFuse */
#define EFUSE_BASE_ESP32S2 0x3F41A000

/* ESP32-S3 eFuse */
#define EFUSE_BASE_ESP32S3 0x60007000

/* ESP32-C2 eFuse */
#define EFUSE_BASE_ESP32C2 0x60008800

/* ESP32-C3 eFuse */
#define EFUSE_BASE_ESP32C3 0x60008800

/* ESP32-C6 eFuse */
#define EFUSE_BASE_ESP32C6 0x600B0800

/* ============================================================================
 * SPI register base addresses per chip type
 * ============================================================================
 */
#define SPI_REG_BASE_ESP8266 0x60000200
#define SPI_REG_BASE_ESP32 0x3FF42000
#define SPI_REG_BASE_ESP32S2 0x3F402000
#define SPI_REG_BASE_ESP32S3 0x60002000 /* Also used by ESP32-C2/C3 */
#define SPI_REG_BASE_ESP32C6 0x60003000

/* ============================================================================
 * UART register addresses per chip type
 * ============================================================================
 */
#define UART_CLKDIV_REG_ESP32 0x3FF40014
#define UART_CLKDIV_REG_ESP32S2 0x3F400014
#define UART_CLKDIV_REG_ESP8266 0x60000014
#define UART_CLKDIV_MASK 0xFFFFF

/* ESP32 flash size register */
#define FLASH_SIZE_REG_ESP32 0x3F400010

/* ============================================================================
 * Chip ID values (used for READ_REG chip detection via magic value)
 * ============================================================================
 */
#define CHIP_ID_ESP8266 0xFFF0C101
#define CHIP_ID_ESP32 0x00F01D83
#define CHIP_ID_ESP32S2 0x000007C6
#define CHIP_ID_ESP32S3 0x00000009
#define CHIP_ID_ESP32C2 0x7C41A06F
#define CHIP_ID_ESP32C3 0x6921506F
#define CHIP_ID_ESP32C6 0x2CE0806F

/* ============================================================================
 * IMAGE_CHIP_ID values (used for GET_SECURITY_INFO chip detection)
 * These are small integers returned in the security info response.
 * ============================================================================
 */
#define IMAGE_CHIP_ID_ESP8266 0
#define IMAGE_CHIP_ID_ESP32 0
#define IMAGE_CHIP_ID_ESP32S2 2
#define IMAGE_CHIP_ID_ESP32S3 9
#define IMAGE_CHIP_ID_ESP32C2 12
#define IMAGE_CHIP_ID_ESP32C3 5
#define IMAGE_CHIP_ID_ESP32C6 13

/* ============================================================================
 * MAC eFuse offsets per chip type
 * ============================================================================
 */

/* ESP8266 MAC eFuse offsets */
#define MAC_EFUSE_WORD0_ESP8266                                                \
    (EFUSE_RD_REG_BASE_ESP8266 + 0x00) /* 0x3FF00050 */
#define MAC_EFUSE_WORD1_ESP8266                                                \
    (EFUSE_RD_REG_BASE_ESP8266 + 0x04) /* 0x3FF00054 */
#define MAC_EFUSE_WORD3_ESP8266                                                \
    (EFUSE_RD_REG_BASE_ESP8266 + 0x0C) /* 0x3FF0005C */

/* ESP32 MAC eFuse offsets */
#define MAC_EFUSE_WORD1_ESP32 (EFUSE_RD_REG_BASE_ESP32 + 0x04) /* 0x3FF5A004   \
                                                                */
#define MAC_EFUSE_WORD2_ESP32 (EFUSE_RD_REG_BASE_ESP32 + 0x08) /* 0x3FF5A008   \
                                                                */

/* ESP32-S2 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32S2 (EFUSE_BASE_ESP32S2 + 0x044) /* 0x3F41A044 */

/* ESP32-S3 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32S3 (EFUSE_BASE_ESP32S3 + 0x044) /* 0x60007044 */

/* ESP32-C2 MAC eFuse offset (note: +0x040, not +0x044) */
#define MAC_EFUSE_BASE_ESP32C2 (EFUSE_BASE_ESP32C2 + 0x040) /* 0x60008840 */

/* ESP32-C3 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32C3 (EFUSE_BASE_ESP32C3 + 0x044) /* 0x60008844 */

/* ESP32-C6 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32C6 (EFUSE_BASE_ESP32C6 + 0x044) /* 0x600B0844 */

/* ============================================================================
 * eFuse encryption field offsets and bit masks
 *
 * These define the locations of encryption-related eFuse fields within
 * the BLOCK0 region. Offsets are from the chip's eFuse base address.
 * ============================================================================
 */

/* ESP32 eFuse encryption fields (EFUSE_RD_REG_BASE = 0x3FF5A000, BLOCK0 at
 * +0x00) */
#define EFUSE_OFFS_FLASH_CRYPT_CNT_ESP32 0x00 /* BLOCK0 word0 */
#define EFUSE_MASK_FLASH_CRYPT_CNT_ESP32 (0x7FUL << 20)
#define EFUSE_OFFS_DISABLE_DL_ENCRYPT_ESP32 0x18 /* BLOCK0 word6 */
#define EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32 (1UL << 7)
#define EFUSE_OFFS_DISABLE_DL_DECRYPT_ESP32 0x18 /* BLOCK0 word6 */
#define EFUSE_BIT_DISABLE_DL_DECRYPT_ESP32 (1UL << 8)
#define EFUSE_OFFS_UART_DOWNLOAD_DIS_ESP32 0x00 /* BLOCK0 word0 */
#define EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32 (1UL << 27)

/* ESP32-S2 eFuse encryption fields (EFUSE_BASE = 0x3F41A000, BLOCK0 at +0x2C)
 */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S2 0x34 /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2 (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S2 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2 (1UL << 4)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S2 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2 (1UL << 5)

/* ESP32-S3 eFuse encryption fields (EFUSE_BASE = 0x60007000, BLOCK0 at +0x2C)
 */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S3 0x34 /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3 (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S3 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S3 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3 (1UL << 4)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S3 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3 (1UL << 5)

/* ESP32-C2 eFuse encryption fields (EFUSE_BASE = 0x60008800, BLOCK0 at +0x2C)
 */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C2 0x30 /* BLOCK0 word1 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2 (7UL << 7)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2 (1UL << 6)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2 (1UL << 14)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2 (1UL << 16)

/* ESP32-C3 eFuse encryption fields (EFUSE_BASE = 0x60008800, BLOCK0 at +0x2C)
 */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C3 0x34 /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3 (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C3 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C3 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3 (1UL << 0)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C3 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3 (1UL << 5)

/* ESP32-C6 eFuse encryption fields (EFUSE_BASE = 0x600B0800, BLOCK0 at +0x2C)
 */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C6 0x34 /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6 (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C6 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C6 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6 (1UL << 0)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C6 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6 (1UL << 5)

/* ============================================================================
 * eFuse KEY_PURPOSE fields (S2/S3/C3/C6 share identical layout)
 *
 * Each key block (KEY0-KEY5) has a 4-bit purpose field in BLOCK0.
 * Hardware selects the key block by scanning KEY_PURPOSE values.
 *
 * Purpose values:
 *   0 = USER, 2 = XTS_AES_256_KEY_1, 3 = XTS_AES_256_KEY_2,
 *   4 = XTS_AES_128_KEY, 5-8 = HMAC_*, 9-11 = SECURE_BOOT_DIGEST
 * ============================================================================
 */

/* KEY_PURPOSE_0 (BLOCK_KEY0) - BLOCK0 word2 bits[27:24] */
#define EFUSE_OFFS_KEY_PURPOSE_0 0x08
#define EFUSE_MASK_KEY_PURPOSE_0 (0x0FUL << 24)
/* KEY_PURPOSE_1 (BLOCK_KEY1) - BLOCK0 word2 bits[31:28] */
#define EFUSE_OFFS_KEY_PURPOSE_1 0x08
#define EFUSE_MASK_KEY_PURPOSE_1 (0x0FUL << 28)
/* KEY_PURPOSE_2 (BLOCK_KEY2) - BLOCK0 word3 bits[3:0] */
#define EFUSE_OFFS_KEY_PURPOSE_2 0x0C
#define EFUSE_MASK_KEY_PURPOSE_2 (0x0FUL << 0)
/* KEY_PURPOSE_3 (BLOCK_KEY3) - BLOCK0 word3 bits[7:4] */
#define EFUSE_OFFS_KEY_PURPOSE_3 0x0C
#define EFUSE_MASK_KEY_PURPOSE_3 (0x0FUL << 4)
/* KEY_PURPOSE_4 (BLOCK_KEY4) - BLOCK0 word3 bits[11:8] */
#define EFUSE_OFFS_KEY_PURPOSE_4 0x0C
#define EFUSE_MASK_KEY_PURPOSE_4 (0x0FUL << 8)
/* KEY_PURPOSE_5 (BLOCK_KEY5) - BLOCK0 word3 bits[15:12] */
#define EFUSE_OFFS_KEY_PURPOSE_5 0x0C
#define EFUSE_MASK_KEY_PURPOSE_5 (0x0FUL << 12)

/* Key purpose values */
#define KEY_PURPOSE_USER 0
#define KEY_PURPOSE_RESERVED 1
#define KEY_PURPOSE_XTS_AES_256_KEY_1 2
#define KEY_PURPOSE_XTS_AES_256_KEY_2 3
#define KEY_PURPOSE_XTS_AES_128_KEY 4
#define KEY_PURPOSE_HMAC_DOWN_ALL 5
#define KEY_PURPOSE_HMAC_DOWN_JTAG 6
#define KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE 7
#define KEY_PURPOSE_HMAC_UP 8
#define KEY_PURPOSE_SECURE_BOOT_DIGEST0 9
#define KEY_PURPOSE_SECURE_BOOT_DIGEST1 10
#define KEY_PURPOSE_SECURE_BOOT_DIGEST2 11

/* ============================================================================
 * eFuse JTAG and Secure Boot fields per chip type
 * ============================================================================
 */

/* ESP32 JTAG and Secure Boot fields */
#define EFUSE_OFFS_JTAG_DISABLE_ESP32 0x18 /* BLOCK0 word6 */
#define EFUSE_BIT_JTAG_DISABLE_ESP32 (1UL << 6)
#define EFUSE_OFFS_ABS_DONE_0_ESP32 0x18 /* BLOCK0 word6 */
#define EFUSE_BIT_ABS_DONE_0_ESP32 (1UL << 4)
#define EFUSE_OFFS_ABS_DONE_1_ESP32 0x18 /* BLOCK0 word6 */
#define EFUSE_BIT_ABS_DONE_1_ESP32 (1UL << 5)

/* ESP32-S2 JTAG and Secure Boot fields */
#define EFUSE_OFFS_DIS_PAD_JTAG_ESP32S2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_PAD_JTAG_ESP32S2 (1UL << 19)
#define EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_SOFT_DIS_JTAG_ESP32S2 (1UL << 17)
#define EFUSE_OFFS_DIS_FORCE_DOWNLOAD_ESP32S2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_FORCE_DOWNLOAD_ESP32S2 (1UL << 12)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE0_ESP32S2 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE0_ESP32S2 (1UL << 21)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE1_ESP32S2 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE1_ESP32S2 (1UL << 22)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE2_ESP32S2 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE2_ESP32S2 (1UL << 23)
#define EFUSE_OFFS_SECURE_BOOT_EN_ESP32S2 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SECURE_BOOT_EN_ESP32S2 (1UL << 20)
#define EFUSE_OFFS_SEC_BOOT_AGG_REVOKE_ESP32S2 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SEC_BOOT_AGG_REVOKE_ESP32S2 (1UL << 21)

/* ESP32-S3 JTAG and Secure Boot fields */
#define EFUSE_OFFS_DIS_PAD_JTAG_ESP32S3 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_PAD_JTAG_ESP32S3 (1UL << 19)
#define EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S3 0x30 /* BLOCK0 word1 */
#define EFUSE_MASK_SOFT_DIS_JTAG_ESP32S3 (7UL << 16)
#define EFUSE_OFFS_DIS_FORCE_DOWNLOAD_ESP32S3 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_FORCE_DOWNLOAD_ESP32S3 (1UL << 12)
#define EFUSE_OFFS_DIS_USB_JTAG_ESP32S3 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_DIS_USB_JTAG_ESP32S3 (1UL << 22)
#define EFUSE_OFFS_DIS_USB_SER_JTAG_ROM_ESP32S3 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_USB_SER_JTAG_ROM_ESP32S3 (1UL << 2)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE0_ESP32S3 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE0_ESP32S3 (1UL << 21)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE1_ESP32S3 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE1_ESP32S3 (1UL << 22)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE2_ESP32S3 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE2_ESP32S3 (1UL << 23)
#define EFUSE_OFFS_SECURE_BOOT_EN_ESP32S3 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SECURE_BOOT_EN_ESP32S3 (1UL << 20)
#define EFUSE_OFFS_SEC_BOOT_AGG_REVOKE_ESP32S3 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SEC_BOOT_AGG_REVOKE_ESP32S3 (1UL << 21)

/* ESP32-C2 JTAG and Secure Boot fields (limited support) */
#define EFUSE_OFFS_DIS_FORCE_DOWNLOAD_ESP32C2 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_FORCE_DOWNLOAD_ESP32C2 (1UL << 14)

/* ESP32-C3 JTAG and Secure Boot fields */
#define EFUSE_OFFS_DIS_PAD_JTAG_ESP32C3 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_PAD_JTAG_ESP32C3 (1UL << 19)
#define EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C3 0x30 /* BLOCK0 word1 */
#define EFUSE_MASK_SOFT_DIS_JTAG_ESP32C3 (7UL << 16)
#define EFUSE_OFFS_DIS_USB_JTAG_ESP32C3 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_USB_JTAG_ESP32C3 (1UL << 9)
#define EFUSE_OFFS_DIS_FORCE_DOWNLOAD_ESP32C3 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_FORCE_DOWNLOAD_ESP32C3 (1UL << 12)
#define EFUSE_OFFS_DIS_USB_SER_JTAG_ROM_ESP32C3 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_USB_SER_JTAG_ROM_ESP32C3 (1UL << 2)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE0_ESP32C3 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE0_ESP32C3 (1UL << 21)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE1_ESP32C3 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE1_ESP32C3 (1UL << 22)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE2_ESP32C3 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE2_ESP32C3 (1UL << 23)
#define EFUSE_OFFS_SECURE_BOOT_EN_ESP32C3 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SECURE_BOOT_EN_ESP32C3 (1UL << 20)
#define EFUSE_OFFS_SEC_BOOT_AGG_REVOKE_ESP32C3 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SEC_BOOT_AGG_REVOKE_ESP32C3 (1UL << 21)

/* ESP32-C6 JTAG and Secure Boot fields */
#define EFUSE_OFFS_DIS_PAD_JTAG_ESP32C6 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_PAD_JTAG_ESP32C6 (1UL << 19)
#define EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C6 0x30 /* BLOCK0 word1 */
#define EFUSE_MASK_SOFT_DIS_JTAG_ESP32C6 (7UL << 16)
#define EFUSE_OFFS_DIS_USB_JTAG_ESP32C6 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_USB_JTAG_ESP32C6 (1UL << 9)
#define EFUSE_OFFS_DIS_FORCE_DOWNLOAD_ESP32C6 0x30 /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_FORCE_DOWNLOAD_ESP32C6 (1UL << 12)
#define EFUSE_OFFS_DIS_USB_SER_JTAG_ROM_ESP32C6 0x3C /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_USB_SER_JTAG_ROM_ESP32C6 (1UL << 2)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE0_ESP32C6 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE0_ESP32C6 (1UL << 21)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE1_ESP32C6 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE1_ESP32C6 (1UL << 22)
#define EFUSE_OFFS_SEC_BOOT_KEY_REVOKE2_ESP32C6 0x34 /* BLOCK0 word2 */
#define EFUSE_BIT_SEC_BOOT_KEY_REVOKE2_ESP32C6 (1UL << 23)
#define EFUSE_OFFS_SECURE_BOOT_EN_ESP32C6 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SECURE_BOOT_EN_ESP32C6 (1UL << 20)
#define EFUSE_OFFS_SEC_BOOT_AGG_REVOKE_ESP32C6 0x38 /* BLOCK0 word3 */
#define EFUSE_BIT_SEC_BOOT_AGG_REVOKE_ESP32C6 (1UL << 21)

/* ============================================================================
 * eFuse controller command registers per chip type
 * Used by espefuse to trigger eFuse program/read operations.
 * When EFUSE_CMD_REG is written with EFUSE_PGM_CMD, the controller
 * programs the eFuse and then clears the command register.
 * ============================================================================
 */

/* ESP32 eFuse controller (registers at EFUSE_RD_REG_BASE, not EFUSE_BASE) */
#define EFUSE_CONF_REG_ESP32 (EFUSE_RD_REG_BASE_ESP32 + 0x0FC)
#define EFUSE_CMD_REG_ESP32 (EFUSE_RD_REG_BASE_ESP32 + 0x104)
#define EFUSE_CONF_WRITE_ESP32 0x5A5A
#define EFUSE_CONF_READ_ESP32 0x5AA5
#define EFUSE_CMD_WRITE_ESP32 0x2
#define EFUSE_CMD_READ_ESP32 0x1

/* ESP32-S2 eFuse controller */
#define EFUSE_CONF_REG_ESP32S2 (EFUSE_BASE_ESP32S2 + 0x1CC)
#define EFUSE_CMD_REG_ESP32S2 (EFUSE_BASE_ESP32S2 + 0x1D4)

/* ESP32-S3 eFuse controller */
#define EFUSE_CONF_REG_ESP32S3 (EFUSE_BASE_ESP32S3 + 0x1CC)
#define EFUSE_CMD_REG_ESP32S3 (EFUSE_BASE_ESP32S3 + 0x1D4)

/* ESP32-C2 eFuse controller */
#define EFUSE_CONF_REG_ESP32C2 (EFUSE_BASE_ESP32C2 + 0x08C)
#define EFUSE_CMD_REG_ESP32C2 (EFUSE_BASE_ESP32C2 + 0x094)

/* ESP32-C3 eFuse controller */
#define EFUSE_CONF_REG_ESP32C3 (EFUSE_BASE_ESP32C3 + 0x1CC)
#define EFUSE_CMD_REG_ESP32C3 (EFUSE_BASE_ESP32C3 + 0x1D4)

/* ESP32-C6 eFuse controller */
#define EFUSE_CONF_REG_ESP32C6 (EFUSE_BASE_ESP32C6 + 0x1CC)
#define EFUSE_CMD_REG_ESP32C6 (EFUSE_BASE_ESP32C6 + 0x1D4)

/* Common eFuse command values (for S2/S3/C2/C3/C6) */
#define EFUSE_WRITE_OP_CODE 0x5A5A
#define EFUSE_READ_OP_CODE 0x5AA5
#define EFUSE_PGM_CMD 0x2
#define EFUSE_READ_CMD 0x1

/* Flash mode constants */
#define FLASH_MODE_QIO 0
#define FLASH_MODE_DIO 2
#define FLASH_MODE_QOUT 1
#define FLASH_MODE_DOUT 3

/* Flash frequency constants */
#define FLASH_FREQ_40M 0
#define FLASH_FREQ_26M 1
#define FLASH_FREQ_20M 2
#define FLASH_FREQ_80M 3

/* Crystal frequency constants */
#define XTAL_FREQ_40M 0
#define XTAL_FREQ_26M 1

/* SPI register count (enough for SPI_CMD through SPI_W15) */
#define SPI_REG_COUNT 64

/* SPI register offsets (common to all chips) */
#define SPI_CMD_OFFS 0x00
#define SPI_ADDR_OFFS 0x04

/* SPI register offsets per chip family.
   Different chip families use different register layouts:
   | Register   | ESP32-S2/S3/C2/C3/C6 | ESP32 | ESP8266 |
   |------------|----------------------|-------|---------|
   | SPI_USR    | 0x18                 | 0x1C  | 0x1C    |
   | SPI_USR1   | 0x1C                 | 0x20  | 0x20    |
   | SPI_USR2   | 0x20                 | 0x24  | 0x24    |
   | SPI_W0     | 0x58                 | 0x80  | 0x40    |
   | SPI_MOSI_DLEN | 0x24              | 0x28  | N/A     |
   | SPI_MISO_DLEN | 0x28              | 0x2C  | N/A     | */
typedef struct {
    BYTE usr;       /* SPI_USR offset */
    BYTE usr1;      /* SPI_USR1 offset */
    BYTE usr2;      /* SPI_USR2 offset */
    BYTE w0;        /* SPI_W0 offset */
    BYTE mosi_dlen; /* SPI_MOSI_DLEN offset (0 if not supported) */
    BYTE miso_dlen; /* SPI_MISO_DLEN offset (0 if not supported) */
} SPI_OFFSETS;

/* SPI register bit definitions */
#define SPI_CMD_USR (1 << 18)
#define SPI_USR_COMMAND (1 << 31)
#define SPI_USR_ADDR (1 << 30)
#define SPI_USR_DUMMY (1 << 29)
#define SPI_USR_MISO (1 << 28)
#define SPI_USR_MOSI (1 << 27)

/* SPI flash commands */
#define SPIFLASH_RDID 0x9F /* Read JEDEC ID */

/* Supported chip types */
typedef enum {
    CHIP_ESP8266, /* ESP8266 WiFi chip */
    CHIP_ESP32,   /* ESP32 dual-core WiFi+BT */
    CHIP_ESP32S2, /* ESP32-S2 single-core WiFi */
    CHIP_ESP32S3, /* ESP32-S3 dual-core WiFi+BT5 */
    CHIP_ESP32C2, /* ESP32-C2 low-cost WiFi */
    CHIP_ESP32C3, /* ESP32-C3 RISC-V WiFi+BT */
    CHIP_ESP32C6, /* ESP32-C6 WiFi 6+BLE 5 */
    CHIP_COUNT
} CHIP_TYPE;

/* Chip characteristics context */
typedef struct {
    CHIP_TYPE type;           /* Chip type */
    char name[CHIP_NAME_MAX]; /* Chip name string */

    BYTE mac[6]; /* MAC address */

    BYTE *efuse;    /* eFuse data (dynamically allocated) */
    int efuse_size; /* eFuse size in bytes */

    DWORD flash_size; /* Flash size in bytes */
    DWORD flash_id;   /* Flash JEDEC ID */
    BYTE xtal_freq;   /* Crystal frequency */

    DWORD sector_size; /* Flash sector size */
    DWORD block_size;  /* Flash block size */
    DWORD page_size;   /* Flash page size */

    DWORD chip_id; /* Chip ID register value (magic value for READ_REG) */
    DWORD security_chip_id; /* IMAGE_CHIP_ID for GET_SECURITY_INFO */
    DWORD pkg_version;      /* Package version */
    BOOL has_usb;           /* USB support flag */

    DWORD spi_reg_base;          /* SPI register base address */
    const SPI_OFFSETS *spi_offs; /* SPI register offsets for this chip family */
    DWORD spi_regs[SPI_REG_COUNT]; /* SPI register file */

    /* eFuse controller simulation */
    DWORD efuse_base;   /* eFuse base address for this chip */
    DWORD pgm_data[32]; /* PGM_DATA staging area for burn (ESP32: 4 blocks × 8
                           words) */
    DWORD efuse_conf_ofs; /* CONF_REG offset from efuse_base (0 = no controller)
                           */
    DWORD efuse_cmd_ofs;  /* CMD_REG offset from efuse_base */
} CHIP_CTX;

/*
 * Chip_Init - Initialize chip context with type-specific defaults
 */
BOOL Chip_Init(CHIP_CTX *ctx, CHIP_TYPE type);

/*
 * Chip_Close - Release chip resources (free eFuse memory)
 */
void Chip_Close(CHIP_CTX *ctx);

/*
 * Chip_GetName - Get chip name string
 */
const char *Chip_GetName(const CHIP_CTX *ctx);

/*
 * Chip_SetMac - Set MAC address
 */
BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6]);

/*
 * Chip_GetMac - Get MAC address
 */
const BYTE *Chip_GetMac(const CHIP_CTX *ctx);

/*
 * Chip_ReadReg - Read register value (supports eFuse address range)
 */
DWORD Chip_ReadReg(const CHIP_CTX *ctx, DWORD addr);

/*
 * Chip_WriteReg - Write register value (eFuse OR operation)
 */
BOOL Chip_WriteReg(CHIP_CTX *ctx, DWORD addr, DWORD val);

/*
 * Chip_SetFlashSize - Set flash size
 */
void Chip_SetFlashSize(CHIP_CTX *ctx, DWORD size);

/*
 * Chip_GetFlashSize - Get flash size
 */
DWORD Chip_GetFlashSize(const CHIP_CTX *ctx);

/*
 * Chip_GetChipId - Get chip ID
 */
DWORD Chip_GetChipId(const CHIP_CTX *ctx);

/*
 * Chip_GetEfuse - Get pointer to eFuse data
 *
 * Returns pointer to eFuse byte array, or NULL if not allocated.
 */
const BYTE *Chip_GetEfuse(const CHIP_CTX *ctx);

/*
 * Chip_GetEfuseMut - Get mutable pointer to eFuse data
 *
 * Returns pointer to eFuse byte array for writing, or NULL if not allocated.
 * Use with caution - eFuse is one-time-programmable in real hardware.
 */
BYTE *Chip_GetEfuseMut(CHIP_CTX *ctx);

/*
 * Chip_GetEfuseSize - Get eFuse size in bytes
 */
int Chip_GetEfuseSize(const CHIP_CTX *ctx);

/*
 * Chip_GetBootBaudRate - Get boot message baud rate
 *
 * Depends on chip type and crystal frequency.
 */
DWORD Chip_GetBootBaudRate(const CHIP_CTX *ctx);

/*
 * Chip_GetBootMessage - Get boot message text for reset
 *
 * Writes chip-specific boot message to caller-provided buffer.
 * download_mode: TRUE for download mode entry, FALSE for normal flash boot
 * reset_cause: 0x01=POWERON, 0x02=EXT, 0x03=WDT
 *
 * @ctx:           Pointer to chip context
 * @download_mode: TRUE for download, FALSE for normal boot
 * @reset_cause:   Reset cause code
 * @buf:           Output buffer
 * @buf_size:      Size of output buffer
 *
 * Returns pointer to buf, or empty string if buffer is too small.
 */
const char *Chip_GetBootMessage(const CHIP_CTX *ctx, BOOL download_mode,
                                BYTE reset_cause, char *buf, size_t buf_size);

/*
 * Chip_GetFlashCryptCnt - Get flash encryption counter value from eFuse
 *
 * Returns the raw bitfield value. Check if odd number of 1-bits
 * to determine if encryption is enabled.
 */
DWORD Chip_GetFlashCryptCnt(const CHIP_CTX *ctx);

/*
 * Chip_IsFlashEncryptionEnabled - Check if flash encryption is active
 *
 * Returns TRUE if flash_crypt_cnt has an odd number of 1-bits.
 */
BOOL Chip_IsFlashEncryptionEnabled(const CHIP_CTX *ctx);

/*
 * Chip_IsDownloadEncryptDisabled - Check if manual encrypt is disabled (release
 * mode)
 *
 * Returns TRUE if DISABLE_DL_ENCRYPT / DIS_DOWNLOAD_MANUAL_ENCRYPT is set.
 */
BOOL Chip_IsDownloadEncryptDisabled(const CHIP_CTX *ctx);

/*
 * Chip_IsDownloadDecryptDisabled - Check if flash decryption is disabled in
 * download mode
 *
 * Returns TRUE if DISABLE_DL_DECRYPT is set (ESP32 only).
 * Other chips do not have this field, always returns FALSE.
 */
BOOL Chip_IsDownloadDecryptDisabled(const CHIP_CTX *ctx);

/*
 * Chip_IsDownloadModeDisabled - Check if download mode is disabled
 *
 * Returns TRUE if UART_DOWNLOAD_DIS / DIS_DOWNLOAD_MODE is set.
 */
BOOL Chip_IsDownloadModeDisabled(const CHIP_CTX *ctx);

/*
 * Chip_IsSecureDownloadEnabled - Check if secure download mode is active
 *
 * Returns TRUE if ENABLE_SECURITY_DOWNLOAD is set.
 * ESP32 does not support this feature.
 */
BOOL Chip_IsSecureDownloadEnabled(const CHIP_CTX *ctx);

/*
 * Chip_GetDlEncryptDisabled - Get raw eFuse value for download encrypt disabled
 *
 * Returns raw eFuse bit value (0 or 1) for DISABLE_DL_ENCRYPT /
 * DIS_DOWNLOAD_MANUAL_ENCRYPT.
 */
DWORD Chip_GetDlEncryptDisabled(const CHIP_CTX *ctx);

/*
 * Chip_GetDlModeDisabled - Get raw eFuse value for download mode disabled
 *
 * Returns raw eFuse bit value (0 or 1) for UART_DOWNLOAD_DIS /
 * DIS_DOWNLOAD_MODE.
 */
DWORD Chip_GetDlModeDisabled(const CHIP_CTX *ctx);

/*
 * Chip_GetSecureBootFlag - Get raw eFuse value for secure boot
 *
 * ESP32: returns ABS_DONE_0 | (ABS_DONE_1 << 1).
 * Others: returns SECURE_BOOT_EN bit value.
 */
DWORD Chip_GetSecureBootFlag(const CHIP_CTX *ctx);

/*
 * Chip_GetJtagFlag - Get raw eFuse value for JTAG disable
 *
 * Returns raw eFuse bit value (0 or 1) for JTAG_DISABLE / DIS_PAD_JTAG.
 */
DWORD Chip_GetJtagFlag(const CHIP_CTX *ctx);

/*
 * Chip_IsSecureBootEnabled - Check if secure boot is enabled
 *
 * ESP32: ABS_DONE_0 or ABS_DONE_1 (Secure Boot V1/V2).
 * ESP32-S2/S3/C3/C6: SECURE_BOOT_EN.
 * ESP8266/C2: not supported, returns FALSE.
 */
BOOL Chip_IsSecureBootEnabled(const CHIP_CTX *ctx);

/*
 * Chip_IsJtagDisabled - Check if JTAG is disabled via eFuse
 *
 * ESP32: JTAG_DISABLE.
 * ESP32-S2/S3/C3/C6: DIS_PAD_JTAG.
 * ESP8266/C2: not supported, returns FALSE.
 */
BOOL Chip_IsJtagDisabled(const CHIP_CTX *ctx);

/*
 * Chip_GetJtagDisabledCount - Get number of disabled JTAG interfaces
 *
 * Returns count of active JTAG disable bits:
 * ESP32: 0 or 1 (JTAG_DISABLE).
 * ESP32-S2: 0-2 (DIS_PAD_JTAG + SOFT_DIS_JTAG).
 * ESP32-S3/C3/C6: 0-3 (DIS_PAD_JTAG + SOFT_DIS_JTAG + DIS_USB_JTAG).
 * ESP8266/C2: 0.
 *
 * Use with Chip_GetJtagTotalCount() to determine partial/full disable.
 */
int Chip_GetJtagDisabledCount(const CHIP_CTX *ctx);

/*
 * Chip_GetJtagTotalCount - Get total number of JTAG interfaces
 *
 * ESP32: 1. ESP32-S2: 2. ESP32-S3/C3/C6: 3. Others: 0.
 */
int Chip_GetJtagTotalCount(const CHIP_CTX *ctx);

/*
 * Chip_GetSoftJtagFlag - Get raw eFuse value for SOFT_DIS_JTAG
 *
 * ESP32-S2: single bit (0 or 1).
 * ESP32-S3/C3/C6: 3-bit counter.
 * Others: 0.
 */
DWORD Chip_GetSoftJtagFlag(const CHIP_CTX *ctx);

/*
 * Chip_GetUsbJtagFlag - Get raw eFuse value for DIS_USB_JTAG
 *
 * ESP32-S3/C3/C6: single bit (0 or 1).
 * Others: 0.
 */
DWORD Chip_GetUsbJtagFlag(const CHIP_CTX *ctx);

/*
 * Chip_GetKeyPurpose - Get key purpose for a key block
 *
 * @ctx:   Chip context
 * @block: Key block index (0 = KEY0, 1 = KEY1, etc.)
 *
 * Returns key purpose value (0 = empty/USER, 4 = XTS_AES_128_KEY).
 * Checks if the key block has non-zero data; returns XTS_AES_128_KEY for
 * BLOCK_KEY0 when programmed, 0 otherwise.
 */
BYTE Chip_GetKeyPurpose(const CHIP_CTX *ctx, int block);

/*
 * Chip_SetKeyPurpose - Set key block purpose in eFuse
 *
 * For S2/S3/C3/C6: writes KEY_PURPOSE_N field.
 * For ESP32/C2: no-op (fixed key assignments).
 * ESP32-S3/C3/C6 KEY5: rejects XTS_AES purposes (hardware bug).
 * Simulator only: directly modifies eFuse array.
 */
void Chip_SetKeyPurpose(CHIP_CTX *ctx, int block, BYTE purpose);

/*
 * Chip_GetEncryptionKeyOffset - Get eFuse offset and length of flash encryption
 * key
 *
 * @ctx:      Chip context
 * @key_len:  Output parameter receiving key length in bytes (16 or 32)
 *
 * Returns byte offset within eFuse array for the encryption key, or -1 if
 * the chip type has no encryption key defined.
 */
int Chip_GetEncryptionKeyOffset(const CHIP_CTX *ctx, int *key_len);

/*
 * Chip_SetFlashEncryption - Set flash encryption state via eFuse
 *
 * @mode: 0 = no encryption, 1 = dev, 2 = release
 * Simulator only: directly modifies eFuse array.
 */
void Chip_SetFlashEncryption(CHIP_CTX *ctx, int mode);

/*
 * Chip_SetDownloadMode - Set download mode state via eFuse
 *
 * @mode: 0 = normal, 1 = secure, 2 = disabled
 * Simulator only: directly modifies eFuse array.
 */
void Chip_SetDownloadMode(CHIP_CTX *ctx, int mode);

/*
 * Chip_ApplyBlock0Defaults - Apply BLOCK0 defaults for missing eFuse fields
 *
 * Fills zero-valued BLOCK0 bytes with chip-specific defaults.
 * Used when loading old .esp files that may not have all BLOCK0 fields.
 */
void Chip_ApplyBlock0Defaults(CHIP_CTX *ctx);

#endif
