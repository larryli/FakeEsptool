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

/* SPI register count (enough for SPI_CMD through SPI_W15) */
#define SPI_REG_COUNT   64

/* SPI register offsets (common layout for ESP32-S2/S3, C2/C3/C6) */
#define SPI_CMD_OFFS        0x00
#define SPI_ADDR_OFFS       0x04
#define SPI_USR_OFFS        0x18
#define SPI_USR1_OFFS       0x1C
#define SPI_USR2_OFFS       0x20
#define SPI_MOSI_DLEN_OFFS  0x24
#define SPI_MISO_DLEN_OFFS  0x28
#define SPI_W0_OFFS         0x58

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
    BYTE flash_mode;            /* Flash SPI mode */
    BYTE flash_freq;            /* Flash SPI frequency */

    DWORD sector_size;          /* Flash sector size */
    DWORD block_size;           /* Flash block size */
    DWORD page_size;            /* Flash page size */

    DWORD chip_id;              /* Chip ID register value */
    DWORD pkg_version;          /* Package version */
    BOOL has_usb;               /* USB support flag */

    DWORD spi_reg_base;         /* SPI register base address */
    DWORD spi_regs[SPI_REG_COUNT]; /* SPI register file */
} CHIP_CTX;

/* Initialize chip context with type-specific defaults */
BOOL Chip_Init(CHIP_CTX *ctx, CHIP_TYPE type);

/* Release chip resources (free eFuse memory) */
void Chip_Close(CHIP_CTX *ctx);

/* Get chip name string */
const char *Chip_GetName(const CHIP_CTX *ctx);

/* Set MAC address */
BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6]);

/* Get MAC address */
const BYTE *Chip_GetMac(const CHIP_CTX *ctx);

/* Read register value (supports eFuse address range) */
DWORD Chip_ReadReg(const CHIP_CTX *ctx, DWORD addr);

/* Write register value (eFuse OR operation) */
BOOL Chip_WriteReg(CHIP_CTX *ctx, DWORD addr, DWORD val);

/* Set flash size */
void Chip_SetFlashSize(CHIP_CTX *ctx, DWORD size);

/* Get flash size */
DWORD Chip_GetFlashSize(const CHIP_CTX *ctx);

/* Get chip ID */
DWORD Chip_GetChipId(const CHIP_CTX *ctx);

/* Get eFuse data pointer */
const BYTE *Chip_GetEfuse(const CHIP_CTX *ctx);

/* Get eFuse size in bytes */
int Chip_GetEfuseSize(const CHIP_CTX *ctx);

#endif
