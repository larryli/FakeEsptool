/*
 * efuse.h - eFuse field definitions and constants for ESP chips.
 */

#ifndef ESP_EFUSE_H
#define ESP_EFUSE_H

#include <windows.h>

/* Forward declaration to avoid circular dependency with chip.h */
struct CHIP_CTX_TAG;
typedef struct CHIP_CTX_TAG CHIP_CTX;

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

/* ============================================================================
 * eFuse query and manipulation functions
 * ============================================================================
 */

DWORD Efuse_GetFlashCryptCnt(const CHIP_CTX *ctx);
BOOL Efuse_IsFlashEncryptionEnabled(const CHIP_CTX *ctx);
BOOL Efuse_IsDownloadEncryptDisabled(const CHIP_CTX *ctx);
BOOL Efuse_IsDownloadDecryptDisabled(const CHIP_CTX *ctx);
BOOL Efuse_IsDownloadModeDisabled(const CHIP_CTX *ctx);
BOOL Efuse_IsSecureDownloadEnabled(const CHIP_CTX *ctx);
DWORD Efuse_GetDlEncryptDisabled(const CHIP_CTX *ctx);
DWORD Efuse_GetDlModeDisabled(const CHIP_CTX *ctx);
DWORD Efuse_GetSecureBootFlag(const CHIP_CTX *ctx);
DWORD Efuse_GetJtagFlag(const CHIP_CTX *ctx);
BOOL Efuse_IsSecureBootEnabled(const CHIP_CTX *ctx);
BOOL Efuse_IsJtagDisabled(const CHIP_CTX *ctx);
int Efuse_GetJtagDisabledCount(const CHIP_CTX *ctx);
int Efuse_GetJtagTotalCount(const CHIP_CTX *ctx);
DWORD Efuse_GetSoftJtagFlag(const CHIP_CTX *ctx);
DWORD Efuse_GetUsbJtagFlag(const CHIP_CTX *ctx);
BYTE Efuse_GetKeyPurpose(const CHIP_CTX *ctx, int block);
void Efuse_SetKeyPurpose(CHIP_CTX *ctx, int block, BYTE purpose);
int Efuse_GetEncryptionKeyOffset(const CHIP_CTX *ctx, int *key_len);
void Efuse_SetFlashEncryption(CHIP_CTX *ctx, int mode);
void Efuse_SetDownloadMode(CHIP_CTX *ctx, int mode);
void Efuse_ApplyBlock0Defaults(CHIP_CTX *ctx);

#endif
