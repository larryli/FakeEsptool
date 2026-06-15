/*
 * chip.h - ESP chip characteristics
 *
 * Simulates chip properties, eFuse, and register access.
 */

#ifndef ESP_CHIP_H
#define ESP_CHIP_H

#include <windows.h>

/* Maximum chip name length */
#define CHIP_NAME_MAX   32

/* ============================================================================
 * Chip detection register (used by esptool for autodetect)
 * ============================================================================ */
#define CHIP_DETECT_REG             0x40001000

/* ============================================================================
 * eFuse base addresses per chip type
 * ============================================================================ */

/* ESP8266 eFuse */
#define EFUSE_RD_REG_BASE_ESP8266   0x3FF00050

/* ESP32 eFuse */
#define EFUSE_BASE_ESP32            0x3FF00000  /* Direct access */
#define EFUSE_RD_REG_BASE_ESP32     0x3FF5A000  /* esptool readEfuse */

/* ESP32-S2 eFuse */
#define EFUSE_BASE_ESP32S2          0x3F41A000

/* ESP32-S3 eFuse */
#define EFUSE_BASE_ESP32S3          0x60007000

/* ESP32-C2 eFuse */
#define EFUSE_BASE_ESP32C2          0x60008800

/* ESP32-C3 eFuse */
#define EFUSE_BASE_ESP32C3          0x60008800

/* ESP32-C6 eFuse */
#define EFUSE_BASE_ESP32C6          0x600B0800

/* ============================================================================
 * SPI register base addresses per chip type
 * ============================================================================ */
#define SPI_REG_BASE_ESP8266        0x60000200
#define SPI_REG_BASE_ESP32          0x3FF42000
#define SPI_REG_BASE_ESP32S2        0x3F402000
#define SPI_REG_BASE_ESP32S3        0x60002000  /* Also used by ESP32-C2/C3 */
#define SPI_REG_BASE_ESP32C6        0x60003000

/* ============================================================================
 * UART register addresses per chip type
 * ============================================================================ */
#define UART_CLKDIV_REG_ESP32       0x3FF40014
#define UART_CLKDIV_REG_ESP32S2     0x3F400014
#define UART_CLKDIV_REG_ESP8266     0x60000014
#define UART_CLKDIV_MASK            0xFFFFF

/* ESP32 flash size register */
#define FLASH_SIZE_REG_ESP32        0x3F400010

/* ============================================================================
 * Chip ID values (used for READ_REG chip detection via magic value)
 * ============================================================================ */
#define CHIP_ID_ESP8266             0xFFF0C101
#define CHIP_ID_ESP32               0x00F01D83
#define CHIP_ID_ESP32S2             0x000007C6
#define CHIP_ID_ESP32S3             0x00000009
#define CHIP_ID_ESP32C2             0x7C41A06F
#define CHIP_ID_ESP32C3             0x6921506F
#define CHIP_ID_ESP32C6             0x2CE0806F

/* ============================================================================
 * IMAGE_CHIP_ID values (used for GET_SECURITY_INFO chip detection)
 * These are small integers returned in the security info response.
 * ============================================================================ */
#define IMAGE_CHIP_ID_ESP8266       0
#define IMAGE_CHIP_ID_ESP32         0
#define IMAGE_CHIP_ID_ESP32S2       2
#define IMAGE_CHIP_ID_ESP32S3       9
#define IMAGE_CHIP_ID_ESP32C2       12
#define IMAGE_CHIP_ID_ESP32C3       5
#define IMAGE_CHIP_ID_ESP32C6       13

/* ============================================================================
 * MAC eFuse offsets per chip type
 * ============================================================================ */

/* ESP8266 MAC eFuse offsets */
#define MAC_EFUSE_WORD0_ESP8266     (EFUSE_RD_REG_BASE_ESP8266 + 0x00)  /* 0x3FF00050 */
#define MAC_EFUSE_WORD1_ESP8266     (EFUSE_RD_REG_BASE_ESP8266 + 0x04)  /* 0x3FF00054 */
#define MAC_EFUSE_WORD3_ESP8266     (EFUSE_RD_REG_BASE_ESP8266 + 0x0C)  /* 0x3FF0005C */

/* ESP32 MAC eFuse offsets */
#define MAC_EFUSE_WORD1_ESP32       (EFUSE_RD_REG_BASE_ESP32 + 0x04)    /* 0x3FF5A004 */
#define MAC_EFUSE_WORD2_ESP32       (EFUSE_RD_REG_BASE_ESP32 + 0x08)    /* 0x3FF5A008 */

/* ESP32-S2 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32S2      (EFUSE_BASE_ESP32S2 + 0x044)        /* 0x3F41A044 */

/* ESP32-S3 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32S3      (EFUSE_BASE_ESP32S3 + 0x044)        /* 0x60007044 */

/* ESP32-C2 MAC eFuse offset (note: +0x040, not +0x044) */
#define MAC_EFUSE_BASE_ESP32C2      (EFUSE_BASE_ESP32C2 + 0x040)        /* 0x60008840 */

/* ESP32-C3 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32C3      (EFUSE_BASE_ESP32C3 + 0x044)        /* 0x60008844 */

/* ESP32-C6 MAC eFuse offset */
#define MAC_EFUSE_BASE_ESP32C6      (EFUSE_BASE_ESP32C6 + 0x044)        /* 0x600B0844 */

/* ============================================================================
 * eFuse encryption field offsets and bit masks
 *
 * These define the locations of encryption-related eFuse fields within
 * the BLOCK0 region. Offsets are from the chip's eFuse base address.
 * ============================================================================ */

/* ESP32 eFuse encryption fields (EFUSE_RD_REG_BASE = 0x3FF5A000, BLOCK0 at +0x2C) */
#define EFUSE_OFFS_FLASH_CRYPT_CNT_ESP32        0x2C    /* BLOCK0 word0 */
#define EFUSE_MASK_FLASH_CRYPT_CNT_ESP32        (0x7FUL << 20)
#define EFUSE_OFFS_DISABLE_DL_ENCRYPT_ESP32     0x44    /* BLOCK0 word6 */
#define EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32      (1UL << 7)
#define EFUSE_OFFS_UART_DOWNLOAD_DIS_ESP32      0x2C    /* BLOCK0 word0 */
#define EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32       (1UL << 27)

/* ESP32-S2 eFuse encryption fields (EFUSE_BASE = 0x3F41A000, BLOCK0 at +0x5C) */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S2   0x64    /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2   (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S2 0x60   /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S2    0x6C    /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2     (1UL << 4)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S2   0x6C    /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2    (1UL << 5)

/* ESP32-S3 eFuse encryption fields (EFUSE_BASE = 0x60007000, BLOCK0 at +0x5C) */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S3   0x64    /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3   (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S3 0x60   /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S3    0x6C    /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3     (1UL << 4)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S3   0x6C    /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3    (1UL << 5)

/* ESP32-C2 eFuse encryption fields (EFUSE_BASE = 0x60008800, BLOCK0 at +0x2C) */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C2   0x30    /* BLOCK0 word1 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2   (7UL << 7)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C2 0x30   /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2 (1UL << 6)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C2    0x30    /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2     (1UL << 14)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C2   0x30    /* BLOCK0 word1 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2    (1UL << 16)

/* ESP32-C3 eFuse encryption fields (EFUSE_BASE = 0x60008800, BLOCK0 at +0x2C) */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C3   0x34    /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3   (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C3 0x30   /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C3    0x3C    /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3     (1UL << 0)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C3   0x3C    /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3    (1UL << 5)

/* ESP32-C6 eFuse encryption fields (EFUSE_BASE = 0x600B0800, BLOCK0 at +0x2C) */
#define EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C6   0x34    /* BLOCK0 word2 */
#define EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6   (7UL << 18)
#define EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C6 0x30   /* BLOCK0 word1 */
#define EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6 (1UL << 20)
#define EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C6    0x3C    /* BLOCK0 word4 */
#define EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6     (1UL << 0)
#define EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C6   0x3C    /* BLOCK0 word4 */
#define EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6    (1UL << 5)

/* ============================================================================
 * eFuse controller command registers per chip type
 * Used by espefuse to trigger eFuse program/read operations.
 * When EFUSE_CMD_REG is written with EFUSE_PGM_CMD, the controller
 * programs the eFuse and then clears the command register.
 * ============================================================================ */

/* ESP32 eFuse controller */
#define EFUSE_CONF_REG_ESP32        (EFUSE_BASE_ESP32 + 0x03C)
#define EFUSE_CMD_REG_ESP32         (EFUSE_BASE_ESP32 + 0x040)
#define EFUSE_CONF_WRITE_ESP32      0x5A5A
#define EFUSE_CONF_READ_ESP32       0x5AA5
#define EFUSE_CMD_WRITE_ESP32       0x2
#define EFUSE_CMD_READ_ESP32        0x1

/* ESP32-S2 eFuse controller */
#define EFUSE_CONF_REG_ESP32S2      (EFUSE_BASE_ESP32S2 + 0x1CC)
#define EFUSE_CMD_REG_ESP32S2       (EFUSE_BASE_ESP32S2 + 0x1D4)

/* ESP32-S3 eFuse controller */
#define EFUSE_CONF_REG_ESP32S3      (EFUSE_BASE_ESP32S3 + 0x1CC)
#define EFUSE_CMD_REG_ESP32S3       (EFUSE_BASE_ESP32S3 + 0x1D4)

/* ESP32-C2 eFuse controller */
#define EFUSE_CONF_REG_ESP32C2      (EFUSE_BASE_ESP32C2 + 0x08C)
#define EFUSE_CMD_REG_ESP32C2       (EFUSE_BASE_ESP32C2 + 0x094)

/* ESP32-C3 eFuse controller */
#define EFUSE_CONF_REG_ESP32C3      (EFUSE_BASE_ESP32C3 + 0x1CC)
#define EFUSE_CMD_REG_ESP32C3       (EFUSE_BASE_ESP32C3 + 0x1D4)

/* ESP32-C6 eFuse controller */
#define EFUSE_CONF_REG_ESP32C6      (EFUSE_BASE_ESP32C6 + 0x1CC)
#define EFUSE_CMD_REG_ESP32C6       (EFUSE_BASE_ESP32C6 + 0x1D4)

/* Common eFuse command values (for S2/S3/C2/C3/C6) */
#define EFUSE_WRITE_OP_CODE         0x5A5A
#define EFUSE_READ_OP_CODE          0x5AA5
#define EFUSE_PGM_CMD               0x2
#define EFUSE_READ_CMD              0x1

/* Flash mode constants */
#define FLASH_MODE_QIO  0
#define FLASH_MODE_DIO  2
#define FLASH_MODE_QOUT 1
#define FLASH_MODE_DOUT 3

/* Flash frequency constants */
#define FLASH_FREQ_40M  0
#define FLASH_FREQ_26M  1
#define FLASH_FREQ_20M  2
#define FLASH_FREQ_80M  3

/* Crystal frequency constants */
#define XTAL_FREQ_40M   0
#define XTAL_FREQ_26M   1

/* SPI register count (enough for SPI_CMD through SPI_W15) */
#define SPI_REG_COUNT   64

/* SPI register offsets (common to all chips) */
#define SPI_CMD_OFFS        0x00
#define SPI_ADDR_OFFS       0x04

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
    BYTE usr;           /* SPI_USR offset */
    BYTE usr1;          /* SPI_USR1 offset */
    BYTE usr2;          /* SPI_USR2 offset */
    BYTE w0;            /* SPI_W0 offset */
    BYTE mosi_dlen;     /* SPI_MOSI_DLEN offset (0 if not supported) */
    BYTE miso_dlen;     /* SPI_MISO_DLEN offset (0 if not supported) */
} SPI_OFFSETS;

/* SPI register bit definitions */
#define SPI_CMD_USR         (1 << 18)
#define SPI_USR_COMMAND     (1 << 31)
#define SPI_USR_ADDR        (1 << 30)
#define SPI_USR_DUMMY       (1 << 29)
#define SPI_USR_MISO        (1 << 28)
#define SPI_USR_MOSI        (1 << 27)

/* SPI flash commands */
#define SPIFLASH_RDID       0x9F    /* Read JEDEC ID */

/* Supported chip types */
typedef enum {
    CHIP_ESP8266,   /* ESP8266 WiFi chip */
    CHIP_ESP32,     /* ESP32 dual-core WiFi+BT */
    CHIP_ESP32S2,   /* ESP32-S2 single-core WiFi */
    CHIP_ESP32S3,   /* ESP32-S3 dual-core WiFi+BT5 */
    CHIP_ESP32C2,   /* ESP32-C2 low-cost WiFi */
    CHIP_ESP32C3,   /* ESP32-C3 RISC-V WiFi+BT */
    CHIP_ESP32C6,   /* ESP32-C6 WiFi 6+BLE 5 */
    CHIP_COUNT
} CHIP_TYPE;

/* Chip characteristics context */
typedef struct {
    CHIP_TYPE type;             /* Chip type */
    char name[CHIP_NAME_MAX];   /* Chip name string */

    BYTE mac[6];                /* MAC address */

    BYTE *efuse;                /* eFuse data (dynamically allocated) */
    int efuse_size;             /* eFuse size in bytes */

    DWORD flash_size;           /* Flash size in bytes */
    DWORD flash_id;             /* Flash JEDEC ID */
    BYTE xtal_freq;             /* Crystal frequency */

    DWORD sector_size;          /* Flash sector size */
    DWORD block_size;           /* Flash block size */
    DWORD page_size;            /* Flash page size */

    DWORD chip_id;              /* Chip ID register value (magic value for READ_REG) */
    DWORD security_chip_id;     /* IMAGE_CHIP_ID for GET_SECURITY_INFO */
    DWORD pkg_version;          /* Package version */
    BOOL has_usb;               /* USB support flag */

    DWORD spi_reg_base;         /* SPI register base address */
    const SPI_OFFSETS *spi_offs; /* SPI register offsets for this chip family */
    DWORD spi_regs[SPI_REG_COUNT]; /* SPI register file */

    /* eFuse controller simulation */
    DWORD pgm_data[8];          /* PGM_DATA0-7 staging area for burn */
    DWORD efuse_conf_ofs;       /* CONF_REG offset from EFUSE_BASE (0 = no controller) */
    DWORD efuse_cmd_ofs;        /* CMD_REG offset from EFUSE_BASE */
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
 * Returns multi-line ASCII string with \r\n line endings.
 * download_mode: TRUE for download mode entry, FALSE for normal flash boot
 * reset_cause: 0x01=POWERON, 0x02=EXT, 0x03=WDT
 */
const char *Chip_GetBootMessage(const CHIP_CTX *ctx, BOOL download_mode, BYTE reset_cause);

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
 * Chip_IsDownloadEncryptDisabled - Check if manual encrypt is disabled (production mode)
 *
 * Returns TRUE if DISABLE_DL_ENCRYPT / DIS_DOWNLOAD_MANUAL_ENCRYPT is set.
 */
BOOL Chip_IsDownloadEncryptDisabled(const CHIP_CTX *ctx);

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
 * Chip_GetKeyPurpose - Get key purpose for a key block
 *
 * @ctx:   Chip context
 * @block: Key block index (0 = KEY0, 1 = KEY1, etc.)
 *
 * Returns key purpose value (0 = empty/USER, 2 = XTS_AES_128_KEY).
 * Checks if the key block has non-zero data; returns XTS_AES_128_KEY for
 * BLOCK_KEY0 when programmed, 0 otherwise.
 */
BYTE Chip_GetKeyPurpose(const CHIP_CTX *ctx, int block);

#endif
