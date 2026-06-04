/*
 * chip.c - ESP chip characteristics implementation
 *
 * Simulates chip properties, eFuse, and register access.
 */

#include "chip.h"
#include "../utils/trace.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if ENABLE_TRACE
static const char *TAG = "CHIP";
#endif

/* SPI register offsets for ESP32-S2/S3, ESP32-C2/C3/C6 (common layout) */
static const SPI_OFFSETS spi_offs_esp32s2 = {
    .usr       = 0x18,  /* SPI_USR */
    .usr1      = 0x1C,  /* SPI_USR1 */
    .usr2      = 0x20,  /* SPI_USR2 */
    .w0        = 0x58,  /* SPI_W0 */
    .mosi_dlen = 0x24,  /* SPI_MOSI_DLEN */
    .miso_dlen = 0x28,  /* SPI_MISO_DLEN */
};

/* SPI register offsets for ESP32 */
static const SPI_OFFSETS spi_offs_esp32 = {
    .usr       = 0x1C,  /* SPI_USR */
    .usr1      = 0x20,  /* SPI_USR1 */
    .usr2      = 0x24,  /* SPI_USR2 */
    .w0        = 0x80,  /* SPI_W0 */
    .mosi_dlen = 0x28,  /* SPI_MOSI_DLEN */
    .miso_dlen = 0x2C,  /* SPI_MISO_DLEN */
};

/* SPI register offsets for ESP8266 */
static const SPI_OFFSETS spi_offs_esp8266 = {
    .usr       = 0x1C,  /* SPI_USR */
    .usr1      = 0x20,  /* SPI_USR1 */
    .usr2      = 0x24,  /* SPI_USR2 */
    .w0        = 0x40,  /* SPI_W0 */
    .mosi_dlen = 0x00,  /* Not supported on ESP8266 */
    .miso_dlen = 0x00,  /* Not supported on ESP8266 */
};

/* Write chip_id as little-endian bytes at eFuse offset 0x4C
   (where esptool reads EFUSE_RD_REG for chip detection) */
static void WriteChipIdToEfuse(CHIP_CTX *ctx)
{
    if (!ctx->efuse || ctx->efuse_size < 0x50)
        return;
    ctx->efuse[0x4C] = (BYTE)(ctx->chip_id & 0xFF);
    ctx->efuse[0x4D] = (BYTE)((ctx->chip_id >> 8) & 0xFF);
    ctx->efuse[0x4E] = (BYTE)((ctx->chip_id >> 16) & 0xFF);
    ctx->efuse[0x4F] = (BYTE)((ctx->chip_id >> 24) & 0xFF);
}

static void InitEsp8266(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP8266");
    ctx->chip_id = 0xFFF0C101;
    ctx->pkg_version = 0;
    ctx->efuse_size = 96;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = FALSE;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);

    /* ESP8266 MAC eFuse layout (esptool-js compatible):
       word0 (0x3FF00050): NIC[2]=MAC[5] at byte[3], rest unused
       word1 (0x3FF00054): NIC[0]=MAC[3] at byte[1], NIC[1]=MAC[4] at byte[0]
       word3 (0x3FF0005C): OUI source (0 = use default, else custom OUI) */
    ctx->efuse[0x53] = ctx->mac[5];  /* byte[3] of word0: (mac0 >> 24) = MAC[5] */
    ctx->efuse[0x54] = ctx->mac[4];  /* byte[0] of word1: mac1 & 0xFF = MAC[4] */
    ctx->efuse[0x55] = ctx->mac[3];  /* byte[1] of word1: (mac1 >> 8) = MAC[3] */
    /* word3 for custom OUI: [mac[2], mac[1], mac[0]] in little-endian */
    ctx->efuse[0x5C] = ctx->mac[2];  /* byte[0] of word3: OUI byte 2 */
    ctx->efuse[0x5D] = ctx->mac[1];  /* byte[1] of word3: OUI byte 1 */
    ctx->efuse[0x5E] = ctx->mac[0];  /* byte[2] of word3: OUI byte 0 */
}

static void InitEsp32(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32");
    ctx->chip_id = 0x00F01D83;
    ctx->pkg_version = 0;
    ctx->efuse_size = 64;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = FALSE;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);

    /* chip_id at EFUSE_RD_REG_BASE(0x3FF5A000) + 0x000 */
    ctx->efuse[0] = 0x83;
    ctx->efuse[1] = 0x1D;
    ctx->efuse[2] = 0xF0;
    ctx->efuse[3] = 0x00;

    /* MAC at EFUSE_RD_REG_BASE + 4 (word1): mac[5],mac[4],mac[3],mac[2] */
    ctx->efuse[4] = ctx->mac[5];
    ctx->efuse[5] = ctx->mac[4];
    ctx->efuse[6] = ctx->mac[3];
    ctx->efuse[7] = ctx->mac[2];
    /* MAC at EFUSE_RD_REG_BASE + 8 (word2): mac[1],mac[0],0x00,0x00 */
    ctx->efuse[8] = ctx->mac[1];
    ctx->efuse[9] = ctx->mac[0];

    WriteChipIdToEfuse(ctx);
}

static void InitEsp32S2(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-S2");
    ctx->chip_id = 0x000007C6;
    ctx->pkg_version = 0;
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
    ctx->efuse[0] = 0xC6;
    ctx->efuse[1] = 0x07;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;

    WriteChipIdToEfuse(ctx);
}

static void InitEsp32S3(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-S3");
    ctx->chip_id = 0x00000009;
    ctx->pkg_version = 0;
    ctx->efuse_size = 256;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
    ctx->efuse[0] = 0x09;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;

    WriteChipIdToEfuse(ctx);
}

static void InitEsp32C2(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-C2");
    ctx->chip_id = 0x7C41A06F;
    ctx->pkg_version = 0;
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = FALSE;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
    ctx->efuse[0] = 0x6F;
    ctx->efuse[1] = 0xA0;
    ctx->efuse[2] = 0x41;
    ctx->efuse[3] = 0x7C;

    /* MAC at EFUSE_BASE(0x60008800) + 0x040 */
    ctx->efuse[0x40] = ctx->mac[5];
    ctx->efuse[0x41] = ctx->mac[4];
    ctx->efuse[0x42] = ctx->mac[3];
    ctx->efuse[0x43] = ctx->mac[2];
    ctx->efuse[0x44] = ctx->mac[1];
    ctx->efuse[0x45] = ctx->mac[0];

    WriteChipIdToEfuse(ctx);
}

static void InitEsp32C3(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-C3");
    ctx->chip_id = 0x6921506F;
    ctx->pkg_version = 0;
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
    ctx->efuse[0] = 0x6F;
    ctx->efuse[1] = 0x50;
    ctx->efuse[2] = 0x21;
    ctx->efuse[3] = 0x69;

    /* MAC at EFUSE_BASE(0x60008800) + 0x044 */
    ctx->efuse[0x44] = ctx->mac[5];
    ctx->efuse[0x45] = ctx->mac[4];
    ctx->efuse[0x46] = ctx->mac[3];
    ctx->efuse[0x47] = ctx->mac[2];
    ctx->efuse[0x48] = ctx->mac[1];
    ctx->efuse[0x49] = ctx->mac[0];

    WriteChipIdToEfuse(ctx);
}

static void InitEsp32C6(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-C6");
    ctx->chip_id = 0x2CE0806F;
    ctx->pkg_version = 0;
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
    ctx->efuse[0] = 0x6F;
    ctx->efuse[1] = 0x80;
    ctx->efuse[2] = 0xE0;
    ctx->efuse[3] = 0x2C;

    /* MAC at EFUSE_BASE(0x600b0800) + 0x044 */
    ctx->efuse[0x44] = ctx->mac[5];
    ctx->efuse[0x45] = ctx->mac[4];
    ctx->efuse[0x46] = ctx->mac[3];
    ctx->efuse[0x47] = ctx->mac[2];
    ctx->efuse[0x48] = ctx->mac[1];
    ctx->efuse[0x49] = ctx->mac[0];

    WriteChipIdToEfuse(ctx);
}

BOOL Chip_Init(CHIP_CTX *ctx, CHIP_TYPE type)
{
    memset(ctx, 0, sizeof(CHIP_CTX));
    ctx->type = type;
    ctx->xtal_freq = XTAL_FREQ_40M;

    ctx->mac[0] = 0xAA;
    ctx->mac[1] = 0xBB;
    ctx->mac[2] = 0xCC;
    ctx->mac[3] = 0xDD;
    ctx->mac[4] = 0xEE;
    ctx->mac[5] = 0x01;

    /* Set SPI register base address and offsets by chip type */
    switch (type) {
    case CHIP_ESP8266:
        ctx->spi_reg_base = 0x60000200;
        ctx->spi_offs = &spi_offs_esp8266;
        break;
    case CHIP_ESP32:
        ctx->spi_reg_base = 0x3FF42000;
        ctx->spi_offs = &spi_offs_esp32;
        break;
    case CHIP_ESP32S2:
        ctx->spi_reg_base = 0x3F402000;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    default:
        /* S3, C2, C3, C6 */
        ctx->spi_reg_base = 0x60002000;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    }

    switch (type) {
    case CHIP_ESP8266: InitEsp8266(ctx); break;
    case CHIP_ESP32:   InitEsp32(ctx);   break;
    case CHIP_ESP32S2: InitEsp32S2(ctx); break;
    case CHIP_ESP32S3: InitEsp32S3(ctx); break;
    case CHIP_ESP32C2: InitEsp32C2(ctx); break;
    case CHIP_ESP32C3: InitEsp32C3(ctx); break;
    case CHIP_ESP32C6: InitEsp32C6(ctx); break;
    default:
        TRACE_FW(TAG, "Unknown chip type: %d", type);
        return FALSE;
    }

    /* Set default flash size and ID */
    Chip_SetFlashSize(ctx, 4 * 1024 * 1024);

    /* Initialize SPI register defaults */
    ctx->spi_regs[SPI_CMD_OFFS / 4] = 0;

    TRACE_FW(TAG, "Chip: %s, eFuse: %d bytes, Flash: %lu KB, SPI_BASE: 0x%08lX, SPI_W0: 0x%02X", 
             ctx->name, ctx->efuse_size, ctx->flash_size / 1024, ctx->spi_reg_base, ctx->spi_offs->w0);
    return TRUE;
}

void Chip_Close(CHIP_CTX *ctx)
{
    if (ctx->efuse) {
        free(ctx->efuse);
        ctx->efuse = NULL;
    }
}

const char *Chip_GetName(const CHIP_CTX *ctx)
{
    return ctx->name;
}

BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6])
{
    memcpy(ctx->mac, mac, 6);

    if (!ctx->efuse)
        return TRUE;

    /* Update MAC in eFuse at the correct offset for each chip type */
    switch (ctx->type) {
    case CHIP_ESP8266:
        /* ESP8266 MAC eFuse layout (esptool-js compatible):
           word0 (0x3FF00050): NIC[2]=MAC[5] at byte[3]
           word1 (0x3FF00054): NIC[0]=MAC[3] at byte[1], NIC[1]=MAC[4] at byte[0]
           word3 (0x3FF0005C): OUI source */
        ctx->efuse[0x53] = mac[5];  /* byte[3] of word0: (mac0 >> 24) = MAC[5] */
        ctx->efuse[0x54] = mac[4];  /* byte[0] of word1: mac1 & 0xFF = MAC[4] */
        ctx->efuse[0x55] = mac[3];  /* byte[1] of word1: (mac1 >> 8) = MAC[3] */
        /* word3 for custom OUI */
        ctx->efuse[0x5C] = mac[2];  /* byte[0] of word3: OUI byte 2 */
        ctx->efuse[0x5D] = mac[1];  /* byte[1] of word3: OUI byte 1 */
        ctx->efuse[0x5E] = mac[0];  /* byte[2] of word3: OUI byte 0 */
        break;
    case CHIP_ESP32:
        /* readEfuse(1)=0x3FF5A004: mac[5],mac[4],mac[3],mac[2] */
        ctx->efuse[4] = mac[5];
        ctx->efuse[5] = mac[4];
        ctx->efuse[6] = mac[3];
        ctx->efuse[7] = mac[2];
        /* readEfuse(2)=0x3FF5A008: mac[1],mac[0],0x00,0x00 */
        ctx->efuse[8] = mac[1];
        ctx->efuse[9] = mac[0];
        break;
    case CHIP_ESP32S2:
        /* MAC at EFUSE_BASE(0x3F41A000) + 0x044 */
        ctx->efuse[0x44] = mac[5];
        ctx->efuse[0x45] = mac[4];
        ctx->efuse[0x46] = mac[3];
        ctx->efuse[0x47] = mac[2];
        ctx->efuse[0x48] = mac[1];
        ctx->efuse[0x49] = mac[0];
        break;
    case CHIP_ESP32C2:
        /* MAC at EFUSE_BASE(0x60008800) + 0x040 */
        ctx->efuse[0x40] = mac[5];
        ctx->efuse[0x41] = mac[4];
        ctx->efuse[0x42] = mac[3];
        ctx->efuse[0x43] = mac[2];
        ctx->efuse[0x44] = mac[1];
        ctx->efuse[0x45] = mac[0];
        break;
    default:
        /* C3/C6/S3: MAC at EFUSE_BASE + 0x044 */
        ctx->efuse[0x44] = mac[5];
        ctx->efuse[0x45] = mac[4];
        ctx->efuse[0x46] = mac[3];
        ctx->efuse[0x47] = mac[2];
        ctx->efuse[0x48] = mac[1];
        ctx->efuse[0x49] = mac[0];
        break;
    }

    return TRUE;
}

const BYTE *Chip_GetMac(const CHIP_CTX *ctx)
{
    return ctx->mac;
}

DWORD Chip_ReadReg(const CHIP_CTX *ctx, DWORD addr)
{
    /* ESP32 EFUSE: 0x3FF00000 + offset (ROM direct access) */
    if (addr >= 0x3FF00000 && addr < 0x3FF00000 + (DWORD)ctx->efuse_size) {
        int offset = (int)(addr - 0x3FF00000);
        if (offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] | 
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) | 
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32 EFUSE_RD_REG_BASE: 0x3FF5A000 (esptool readEfuse) */
    if (addr >= 0x3FF5A000 && addr < 0x3FF5A100) {
        int offset = (int)(addr - 0x3FF5A000);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32 flash size register */
    if (addr == 0x3F400010)
        return (ctx->flash_size >> 16) & 0xFFFF;

    /* ESP32-S2 EFUSE: 0x3F41A000 */
    if (addr >= 0x3F41A000 && addr < 0x3F41A100) {
        int offset = (int)(addr - 0x3F41A000);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32-C2/C3 EFUSE: 0x60008800 */
    if (addr >= 0x60008800 && addr < 0x60008900) {
        int offset = (int)(addr - 0x60008800);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32-S3 EFUSE: 0x60007000 */
    if (addr >= 0x60007000 && addr < 0x60007100) {
        int offset = (int)(addr - 0x60007000);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32-C6 EFUSE: 0x600B0800 */
    if (addr >= 0x600B0800 && addr < 0x600B0900) {
        int offset = (int)(addr - 0x600B0800);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* Chip detection magic register (0x40001000) - used by esptool for autodetect */
    if (addr == 0x40001000)
        return ctx->chip_id;

    /* UART clock divider register - used for crystal frequency detection.
       esptool-js calculates: etsXtal = (baudrate * uartDiv) / 1000000 / XTAL_CLK_DIVIDER
       For ESP8266: XTAL_CLK_DIVIDER=2, so uartDiv = APB_CLK / baudrate = (2*xtal) / baudrate
       For other chips: XTAL_CLK_DIVIDER=1, so uartDiv = xtal / baudrate */
    if (addr == 0x60000014 || addr == 0x3FF40014 || addr == 0x3F400014) {
        DWORD xtal;
        /* Fixed 40MHz for C3/C6/S2/S3 */
        if (ctx->type == CHIP_ESP32C3 || ctx->type == CHIP_ESP32C6 ||
            ctx->type == CHIP_ESP32S2 || ctx->type == CHIP_ESP32S3) {
            xtal = 40000000;
        } else {
            /* Dynamic detection for ESP32/C2/ESP8266 */
            switch (ctx->xtal_freq) {
            case XTAL_FREQ_26M: xtal = 26000000; break;
            default:            xtal = 40000000; break;
            }
        }
        if (ctx->type == CHIP_ESP8266)
            return (2 * xtal) / 115200;
        else
            return xtal / 115200;
    }

    /* SPI register read */
    if (ctx->spi_reg_base != 0 &&
        addr >= ctx->spi_reg_base &&
        addr < ctx->spi_reg_base + SPI_REG_COUNT * 4) {
        int offset = (int)(addr - ctx->spi_reg_base);
        int idx = offset / 4;
        if (idx >= 0 && idx < SPI_REG_COUNT) {
            return ctx->spi_regs[idx];
        }
    }

    return 0;
}

BOOL Chip_WriteReg(CHIP_CTX *ctx, DWORD addr, DWORD val)
{
    /* Helper macro for eFuse write at a given base address */
    #define EFUSE_WRITE_AT(base) \
        if (addr >= (base) && addr < (base) + (DWORD)ctx->efuse_size) { \
            int offset = (int)(addr - (base)); \
            if (offset + 3 < ctx->efuse_size) { \
                ctx->efuse[offset] |= (BYTE)(val & 0xFF); \
                ctx->efuse[offset + 1] |= (BYTE)((val >> 8) & 0xFF); \
                ctx->efuse[offset + 2] |= (BYTE)((val >> 16) & 0xFF); \
                ctx->efuse[offset + 3] |= (BYTE)((val >> 24) & 0xFF); \
                TRACE_FW(TAG, "eFuse write: base=0x%08lX offset=0x%X val=0x%08lX", (DWORD)(base), offset, val); \
            } \
            return TRUE; \
        }

    /* ESP32 EFUSE: 0x3FF00000 */
    EFUSE_WRITE_AT(0x3FF00000)

    /* ESP32-S2 EFUSE: 0x3F41A000 */
    EFUSE_WRITE_AT(0x3F41A000)

    /* ESP32-S3 EFUSE: 0x60007000 */
    EFUSE_WRITE_AT(0x60007000)

    /* ESP32-C2/C3 EFUSE: 0x60008800 */
    EFUSE_WRITE_AT(0x60008800)

    /* ESP32-C6 EFUSE: 0x600B0800 */
    EFUSE_WRITE_AT(0x600B0800)

    #undef EFUSE_WRITE_AT

    /* SPI register write */
    if (ctx->spi_reg_base != 0 &&
        addr >= ctx->spi_reg_base &&
        addr < ctx->spi_reg_base + SPI_REG_COUNT * 4) {
        int offset = (int)(addr - ctx->spi_reg_base);
        int idx = offset / 4;
        if (idx >= 0 && idx < SPI_REG_COUNT) {
            ctx->spi_regs[idx] = val;

            /* Simulate SPI command execution when SPI_CMD_USR bit is set */
            if (offset == SPI_CMD_OFFS && (val & SPI_CMD_USR)) {
                DWORD usr2 = ctx->spi_regs[ctx->spi_offs->usr2 / 4];
                DWORD cmd = usr2 & 0xFFFF;

                /* Flash ID read command (0x9F) */
                if (cmd == SPIFLASH_RDID) {
                    ctx->spi_regs[ctx->spi_offs->w0 / 4] = ctx->flash_id;
                    TRACE_FW(TAG, "SPI RDID: flash_id=0x%08lX", ctx->flash_id);
                }

                /* Clear SPI_CMD_USR bit to indicate command complete */
                ctx->spi_regs[SPI_CMD_OFFS / 4] &= ~SPI_CMD_USR;
            }
        }
        return TRUE;
    }

    return TRUE;
}

void Chip_SetFlashSize(CHIP_CTX *ctx, DWORD size)
{
    ctx->flash_size = size;

    /* Update flash_id capacity byte based on size.
       Flash ID format: 0xCCDDMM
         MM = Manufacturer ID
         DD = Device ID high byte (0x40)
         CC = Capacity identifier */
    BYTE cap_id;
    switch (size) {
    case 256 * 1024:      cap_id = 0x12; break;
    case 512 * 1024:      cap_id = 0x13; break;
    case 1 * 1024 * 1024: cap_id = 0x14; break;
    case 2 * 1024 * 1024: cap_id = 0x15; break;
    case 4 * 1024 * 1024: cap_id = 0x16; break;
    case 8 * 1024 * 1024: cap_id = 0x17; break;
    case 16 * 1024 * 1024: cap_id = 0x18; break;
    case 32 * 1024 * 1024: cap_id = 0x19; break;
    default:              cap_id = 0x16; break; /* Default 4MB */
    }

    /* Default manufacturer: Winbond (0xEF)
       Other known manufacturers:
         0xEF = Winbond
         0xC8 = GigaDevice
         0x20 = XMC
         0xC2 = Macronix
         0xE0 = Espressif */
    ctx->flash_id = ((DWORD)cap_id << 16) | 0x40EF;
}

DWORD Chip_GetFlashSize(const CHIP_CTX *ctx)
{
    return ctx->flash_size;
}

DWORD Chip_GetChipId(const CHIP_CTX *ctx)
{
    return ctx->chip_id;
}

const BYTE *Chip_GetEfuse(const CHIP_CTX *ctx)
{
    return ctx->efuse;
}

int Chip_GetEfuseSize(const CHIP_CTX *ctx)
{
    return ctx->efuse_size;
}

DWORD Chip_GetBootBaudRate(const CHIP_CTX *ctx)
{
    if (ctx->type == CHIP_ESP8266)
        return 74880;
    if (ctx->type == CHIP_ESP32C2 && ctx->xtal_freq == XTAL_FREQ_26M)
        return 74880;
    return 115200;
}

static const char *ResetCauseStr(BYTE cause)
{
    switch (cause) {
    case 0x01: return "POWERON";
    case 0x02: return "EXT";
    case 0x03: return "WDT";
    default:   return "UNKNOWN";
    }
}

const char *Chip_GetBootMessage(const CHIP_CTX *ctx, BYTE reset_cause)
{
    const char *rst = ResetCauseStr(reset_cause);
    static char buf[512];

    switch (ctx->type) {
    case CHIP_ESP8266:
        snprintf(buf, sizeof(buf),
            "ets_main.c 542 \r\n"
            "ets_main.c 543 \r\n"
            "rst:0x%02X (%s),boot:0x3 (DOWNLOAD(UART0/1/2))\r\n"
            "waiting for download\r\n",
            reset_cause, rst);
        break;
    case CHIP_ESP32:
        snprintf(buf, sizeof(buf),
            "ESP-ROM:esp32-20210719\r\n"
            "Build:Jul 19 2021\r\n"
            "rst:0x%02X (%s),boot:0x3 (DOWNLOAD(UART0/1/2))\r\n"
            "waiting for download\r\n",
            reset_cause, rst);
        break;
    case CHIP_ESP32S2:
        snprintf(buf, sizeof(buf),
            "ESP-ROM:esp32s2-20210719\r\n"
            "Build:Jul 19 2021\r\n"
            "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
            "waiting for download\r\n",
            reset_cause, rst);
        break;
    case CHIP_ESP32S3:
        snprintf(buf, sizeof(buf),
            "ESP-ROM:esp32s3-20210719\r\n"
            "Build:Jul 19 2021\r\n"
            "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
            "waiting for download\r\n",
            reset_cause, rst);
        break;
    case CHIP_ESP32C2:
        snprintf(buf, sizeof(buf),
            "ESP-ROM:esp8684-api2-20220127\r\n"
            "Build:Jan 27 2022\r\n"
            "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
            "waiting for download\r\n",
            reset_cause, rst);
        break;
    case CHIP_ESP32C3:
        snprintf(buf, sizeof(buf),
            "ESP-ROM:esp32c3-20210719\r\n"
            "Build:Jul 19 2021\r\n"
            "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
            "waiting for download\r\n",
            reset_cause, rst);
        break;
    case CHIP_ESP32C6:
        snprintf(buf, sizeof(buf),
            "ESP-ROM:esp32c6-20210719\r\n"
            "Build:Jul 19 2021\r\n"
            "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
            "waiting for download\r\n",
            reset_cause, rst);
        break;
    default:
        buf[0] = '\0';
        break;
    }

    return buf;
}
