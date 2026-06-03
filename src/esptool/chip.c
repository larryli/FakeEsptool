/*
 * chip.c - ESP chip characteristics implementation
 *
 * Simulates chip properties, eFuse, and register access.
 */

#include "chip.h"
#include "../utils/trace.h"
#include <string.h>
#include <stdlib.h>

#if ENABLE_TRACE
static const char *TAG = "CHIP";
#endif

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
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);

    /* readEfuse(0)=0x3FF00050: mac[5],mac[4],mac[3],0x00 */
    ctx->efuse[0x50] = ctx->mac[5];
    ctx->efuse[0x51] = ctx->mac[4];
    ctx->efuse[0x52] = ctx->mac[3];
    /* readEfuse(1)=0x3FF00054: 0x00,mac[2],mac[1],mac[0] */
    ctx->efuse[0x55] = ctx->mac[2];
    ctx->efuse[0x56] = ctx->mac[1];
    ctx->efuse[0x57] = ctx->mac[0];
    /* readEfuse(3)=0x3FF0005C: 0 (use default OUI) */
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
    ctx->flash_id = 0x1640EF;

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
    /* MAC at EFUSE_RD_REG_BASE + 8 (word2): 0x00,0x00,mac[1],mac[0] */
    ctx->efuse[10] = ctx->mac[1];
    ctx->efuse[11] = ctx->mac[0];

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
    ctx->flash_id = 0x1640EF;

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
    ctx->flash_id = 0x1640EF;

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
    ctx->flash_id = 0x1640EF;

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
    ctx->flash_id = 0x1640EF;

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
    ctx->flash_id = 0x1640EF;

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
    ctx->flash_size = 4 * 1024 * 1024;
    ctx->flash_mode = FLASH_MODE_DIO;
    ctx->flash_freq = FLASH_FREQ_40M;

    ctx->mac[0] = 0xAA;
    ctx->mac[1] = 0xBB;
    ctx->mac[2] = 0xCC;
    ctx->mac[3] = 0xDD;
    ctx->mac[4] = 0xEE;
    ctx->mac[5] = 0x01;

    /* Set SPI register base address by chip type */
    switch (type) {
    case CHIP_ESP8266: ctx->spi_reg_base = 0x60000200; break;
    case CHIP_ESP32:   ctx->spi_reg_base = 0x3FF42000; break;
    case CHIP_ESP32S2: ctx->spi_reg_base = 0x3F402000; break;
    default:           ctx->spi_reg_base = 0x60002000; break; /* S3, C2, C3, C6 */
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

    /* Initialize SPI register defaults */
    ctx->spi_regs[SPI_CMD_OFFS / 4] = 0;

    TRACE_FW(TAG, "Chip: %s, eFuse: %d bytes, Flash: %lu KB, SPI_BASE: 0x%08lX", 
             ctx->name, ctx->efuse_size, ctx->flash_size / 1024, ctx->spi_reg_base);
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
        /* readEfuse(0)=0x3FF00050: mac[5],mac[4],mac[3],0x00 */
        ctx->efuse[0x50] = mac[5];
        ctx->efuse[0x51] = mac[4];
        ctx->efuse[0x52] = mac[3];
        /* readEfuse(1)=0x3FF00054: 0x00,mac[2],mac[1],mac[0] */
        ctx->efuse[0x55] = mac[2];
        ctx->efuse[0x56] = mac[1];
        ctx->efuse[0x57] = mac[0];
        break;
    case CHIP_ESP32:
        /* readEfuse(1)=0x3FF5A004: mac[5],mac[4],mac[3],mac[2] */
        ctx->efuse[4] = mac[5];
        ctx->efuse[5] = mac[4];
        ctx->efuse[6] = mac[3];
        ctx->efuse[7] = mac[2];
        /* readEfuse(2)=0x3FF5A008: 0x00,0x00,mac[1],mac[0] */
        ctx->efuse[10] = mac[1];
        ctx->efuse[11] = mac[0];
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

    /* ESP32 chip ID register */
    if (addr == 0x3FF5A000)
        return ctx->chip_id;

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

    /* ESP32-P4 EFUSE: 0x5012D000 */
    if (addr >= 0x5012D000 && addr < 0x5012D100) {
        int offset = (int)(addr - 0x5012D000);
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
    if (addr == 0x60000014 || addr == 0x3FF40014) {
        DWORD xtal;
        switch (ctx->flash_freq) {
        case 1:  xtal = 26000000; break;
        case 2:  xtal = 20000000; break;
        case 3:  xtal = 80000000; break;
        default: xtal = 40000000; break;
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
    /* ESP32 EFUSE write */
    if (addr >= 0x3FF00000 && addr < 0x3FF00000 + (DWORD)ctx->efuse_size) {
        int offset = (int)(addr - 0x3FF00000);
        if (offset + 3 < ctx->efuse_size) {
            ctx->efuse[offset] |= (BYTE)(val & 0xFF);
            ctx->efuse[offset + 1] |= (BYTE)((val >> 8) & 0xFF);
            ctx->efuse[offset + 2] |= (BYTE)((val >> 16) & 0xFF);
            ctx->efuse[offset + 3] |= (BYTE)((val >> 24) & 0xFF);
            TRACE_FW(TAG, "eFuse write: offset=0x%X val=0x%08lX", offset, val);
        }
        return TRUE;
    }

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
                DWORD usr2 = ctx->spi_regs[SPI_USR2_OFFS / 4];
                DWORD cmd = usr2 & 0xFFFF;

                /* Flash ID read command (0x9F) */
                if (cmd == SPIFLASH_RDID) {
                    ctx->spi_regs[SPI_W0_OFFS / 4] = ctx->flash_id;
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
