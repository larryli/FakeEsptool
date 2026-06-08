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

/* Chip configuration table */
typedef struct {
    const char *name;
    DWORD chip_id;              /* Magic value for READ_REG detection */
    DWORD security_chip_id;     /* IMAGE_CHIP_ID for GET_SECURITY_INFO */
    int efuse_size;
    BOOL has_usb;
    BYTE chip_id_bytes[4];  /* Chip ID as little-endian bytes for eFuse */
} CHIP_CONFIG;

static const CHIP_CONFIG chip_configs[CHIP_COUNT] = {
    [CHIP_ESP8266] = { "ESP8266",  CHIP_ID_ESP8266, IMAGE_CHIP_ID_ESP8266, 96,  FALSE, {0x01, 0xC1, 0xF0, 0xFF} },
    [CHIP_ESP32]   = { "ESP32",    CHIP_ID_ESP32,   IMAGE_CHIP_ID_ESP32,   64,  FALSE, {0x83, 0x1D, 0xF0, 0x00} },
    [CHIP_ESP32S2] = { "ESP32-S2", CHIP_ID_ESP32S2, IMAGE_CHIP_ID_ESP32S2, 128, TRUE,  {0xC6, 0x07, 0x00, 0x00} },
    [CHIP_ESP32S3] = { "ESP32-S3", CHIP_ID_ESP32S3, IMAGE_CHIP_ID_ESP32S3, 256, TRUE,  {0x09, 0x00, 0x00, 0x00} },
    [CHIP_ESP32C2] = { "ESP32-C2", CHIP_ID_ESP32C2, IMAGE_CHIP_ID_ESP32C2, 128, FALSE, {0x6F, 0xA0, 0x41, 0x7C} },
    [CHIP_ESP32C3] = { "ESP32-C3", CHIP_ID_ESP32C3, IMAGE_CHIP_ID_ESP32C3, 128, TRUE,  {0x6F, 0x50, 0x21, 0x69} },
    [CHIP_ESP32C6] = { "ESP32-C6", CHIP_ID_ESP32C6, IMAGE_CHIP_ID_ESP32C6, 128, TRUE,  {0x6F, 0x80, 0xE0, 0x2C} },
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

/* Common chip initialization */
static BOOL InitChipCommon(CHIP_CTX *ctx, CHIP_TYPE type)
{
    const CHIP_CONFIG *cfg = &chip_configs[type];
    
    strcpy(ctx->name, cfg->name);
    ctx->chip_id = cfg->chip_id;
    ctx->security_chip_id = cfg->security_chip_id;
    ctx->pkg_version = 0;
    ctx->efuse_size = cfg->efuse_size;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = cfg->has_usb;

    ctx->efuse = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ctx->efuse_size);
    if (!ctx->efuse) {
        TRACE_FW(TAG, "Failed to allocate eFuse for %s", cfg->name);
        return FALSE;
    }

    /* Write chip ID bytes to eFuse */
    ctx->efuse[0] = cfg->chip_id_bytes[0];
    ctx->efuse[1] = cfg->chip_id_bytes[1];
    ctx->efuse[2] = cfg->chip_id_bytes[2];
    ctx->efuse[3] = cfg->chip_id_bytes[3];

    return TRUE;
}

/* Write MAC address to eFuse for ESP8266 */
static void WriteMacEsp8266(CHIP_CTX *ctx)
{
    /* ESP8266 MAC eFuse layout (esptool-js compatible):
       word0 (MAC_EFUSE_WORD0_ESP8266): NIC[2]=MAC[5] at byte[3], rest unused
       word1 (MAC_EFUSE_WORD1_ESP8266): NIC[0]=MAC[3] at byte[1], NIC[1]=MAC[4] at byte[0]
       word3 (MAC_EFUSE_WORD3_ESP8266): OUI source (0 = use default, else custom OUI) */
    ctx->efuse[0x53] = ctx->mac[5];  /* byte[3] of word0: (mac0 >> 24) = MAC[5] */
    ctx->efuse[0x54] = ctx->mac[4];  /* byte[0] of word1: mac1 & 0xFF = MAC[4] */
    ctx->efuse[0x55] = ctx->mac[3];  /* byte[1] of word1: (mac1 >> 8) = MAC[3] */
    /* word3 for custom OUI: [mac[2], mac[1], mac[0]] in little-endian */
    ctx->efuse[0x5C] = ctx->mac[2];  /* byte[0] of word3: OUI byte 2 */
    ctx->efuse[0x5D] = ctx->mac[1];  /* byte[1] of word3: OUI byte 1 */
    ctx->efuse[0x5E] = ctx->mac[0];  /* byte[2] of word3: OUI byte 0 */
}

/* Write MAC address to eFuse for ESP32 */
static void WriteMacEsp32(CHIP_CTX *ctx)
{
    /* MAC at EFUSE_RD_REG_BASE + 4 (word1): mac[5],mac[4],mac[3],mac[2] */
    ctx->efuse[4] = ctx->mac[5];
    ctx->efuse[5] = ctx->mac[4];
    ctx->efuse[6] = ctx->mac[3];
    ctx->efuse[7] = ctx->mac[2];
    /* MAC at EFUSE_RD_REG_BASE + 8 (word2): mac[1],mac[0],0x00,0x00 */
    ctx->efuse[8] = ctx->mac[1];
    ctx->efuse[9] = ctx->mac[0];
}

/* Write MAC address to eFuse for ESP32-S2/S3/C3/C6 (at offset 0x44) */
static void WriteMacAt0x44(CHIP_CTX *ctx)
{
    ctx->efuse[0x44] = ctx->mac[5];
    ctx->efuse[0x45] = ctx->mac[4];
    ctx->efuse[0x46] = ctx->mac[3];
    ctx->efuse[0x47] = ctx->mac[2];
    ctx->efuse[0x48] = ctx->mac[1];
    ctx->efuse[0x49] = ctx->mac[0];
}

/* Write MAC address to eFuse for ESP32-C2 (at offset 0x40) */
static void WriteMacAt0x40(CHIP_CTX *ctx)
{
    ctx->efuse[0x40] = ctx->mac[5];
    ctx->efuse[0x41] = ctx->mac[4];
    ctx->efuse[0x42] = ctx->mac[3];
    ctx->efuse[0x43] = ctx->mac[2];
    ctx->efuse[0x44] = ctx->mac[1];
    ctx->efuse[0x45] = ctx->mac[0];
}

/* Initialize ESP8266 chip context */
static BOOL InitEsp8266(CHIP_CTX *ctx)
{
    if (!InitChipCommon(ctx, CHIP_ESP8266))
        return FALSE;
    WriteMacEsp8266(ctx);
    return TRUE;
}

/* Initialize ESP32 chip context */
static BOOL InitEsp32(CHIP_CTX *ctx)
{
    if (!InitChipCommon(ctx, CHIP_ESP32))
        return FALSE;
    WriteMacEsp32(ctx);
    WriteChipIdToEfuse(ctx);
    return TRUE;
}

/* Initialize ESP32-S2 chip context */
static BOOL InitEsp32S2(CHIP_CTX *ctx)
{
    if (!InitChipCommon(ctx, CHIP_ESP32S2))
        return FALSE;
    WriteMacAt0x44(ctx);
    WriteChipIdToEfuse(ctx);

    /* Set chip revision to 1.0 in eFuse.
       ESP32-S2: major at BLOCK1 word3 bits[21:20] = byte 0x52 bits[5:4] */
    ctx->efuse[0x52] |= 0x10;  /* major=1 */

    return TRUE;
}

/* Initialize ESP32-S3 chip context */
static BOOL InitEsp32S3(CHIP_CTX *ctx)
{
    if (!InitChipCommon(ctx, CHIP_ESP32S3))
        return FALSE;
    WriteMacAt0x44(ctx);
    WriteChipIdToEfuse(ctx);

    /* Set chip revision to 1.0 in eFuse.
       ESP32-S3: major at BLOCK1 word5 bits[25:24] = byte 0x5A bits[1:0] */
    ctx->efuse[0x5A] |= 0x01;  /* major=1 */

    return TRUE;
}

/* Initialize ESP32-C2 chip context */
static BOOL InitEsp32C2(CHIP_CTX *ctx)
{
    if (!InitChipCommon(ctx, CHIP_ESP32C2))
        return FALSE;
    WriteMacAt0x40(ctx);
    WriteChipIdToEfuse(ctx);

    /* Set chip revision to 1.0 (major=1, minor=0) in eFuse.
       ESP32-C2 reads revision from EFUSE_BLOCK2_ADDR + 4 (offset 0x44):
         major = bits[21:20] = byte 0x46 bits[5:4]
         minor = bits[19:16] = byte 0x46 bits[3:0]
       Revision 0 (ECO0) causes esptool to disable the stub flasher. */
    ctx->efuse[0x46] |= 0x10;  /* major=1, minor=0 */

    return TRUE;
}

/* Initialize ESP32-C3 chip context */
static BOOL InitEsp32C3(CHIP_CTX *ctx)
{
    if (!InitChipCommon(ctx, CHIP_ESP32C3))
        return FALSE;
    WriteMacAt0x44(ctx);
    WriteChipIdToEfuse(ctx);

    /* Set chip revision to 1.0 in eFuse.
       ESP32-C3: major at BLOCK1 word3 bits[19:18] = byte 0x52 bits[3:2] */
    ctx->efuse[0x52] |= 0x04;  /* major=1 */

    return TRUE;
}

/* Initialize ESP32-C6 chip context */
static BOOL InitEsp32C6(CHIP_CTX *ctx)
{
    if (!InitChipCommon(ctx, CHIP_ESP32C6))
        return FALSE;
    WriteMacAt0x44(ctx);
    WriteChipIdToEfuse(ctx);

    /* Set chip revision to 1.0 in eFuse.
       ESP32-C6: major at BLOCK1 word3 bits[19:18] = byte 0x52 bits[3:2] */
    ctx->efuse[0x52] |= 0x04;  /* major=1 */

    return TRUE;
}

/*
 * Chip_Init - Initialize chip context with type-specific defaults
 *
 * Sets up chip properties, allocates eFuse memory, and configures
 * SPI register offsets based on chip type.
 *
 * @ctx:  Pointer to chip context to initialize
 * @type: Chip type enum (CHIP_ESP8266, CHIP_ESP32, etc.)
 *
 * Returns TRUE on success, FALSE on failure (memory allocation error).
 * On failure, any allocated resources are automatically freed.
 */
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
        ctx->spi_reg_base = SPI_REG_BASE_ESP8266;
        ctx->spi_offs = &spi_offs_esp8266;
        break;
    case CHIP_ESP32:
        ctx->spi_reg_base = SPI_REG_BASE_ESP32;
        ctx->spi_offs = &spi_offs_esp32;
        break;
    case CHIP_ESP32S2:
        ctx->spi_reg_base = SPI_REG_BASE_ESP32S2;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    case CHIP_ESP32C6:
        ctx->spi_reg_base = SPI_REG_BASE_ESP32C6;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    default:
        /* S3, C2, C3 */
        ctx->spi_reg_base = SPI_REG_BASE_ESP32S3;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    }

    switch (type) {
    case CHIP_ESP8266: if (!InitEsp8266(ctx)) goto fail; break;
    case CHIP_ESP32:   if (!InitEsp32(ctx))   goto fail; break;
    case CHIP_ESP32S2: if (!InitEsp32S2(ctx)) goto fail; break;
    case CHIP_ESP32S3: if (!InitEsp32S3(ctx)) goto fail; break;
    case CHIP_ESP32C2: if (!InitEsp32C2(ctx)) goto fail; break;
    case CHIP_ESP32C3: if (!InitEsp32C3(ctx)) goto fail; break;
    case CHIP_ESP32C6: if (!InitEsp32C6(ctx)) goto fail; break;
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

fail:
    Chip_Close(ctx);
    return FALSE;
}

/*
 * Chip_Close - Release chip resources
 *
 * Frees dynamically allocated eFuse memory.
 * Safe to call multiple times.
 */
void Chip_Close(CHIP_CTX *ctx)
{
    if (ctx->efuse) {
        HeapFree(GetProcessHeap(), 0, ctx->efuse);
        ctx->efuse = NULL;
    }
}

/*
 * Chip_GetName - Get chip name string
 *
 * Returns pointer to static chip name (e.g. "ESP32", "ESP32-C3").
 */
const char *Chip_GetName(const CHIP_CTX *ctx)
{
    return ctx->name;
}

/*
 * Chip_SetMac - Set MAC address and update eFuse
 *
 * Updates the MAC address in the chip context and writes it to the
 * correct eFuse offset based on chip type.
 *
 * @ctx: Pointer to chip context
 * @mac: 6-byte MAC address array
 *
 * Returns TRUE on success.
 */
BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6])
{
    memcpy(ctx->mac, mac, 6);

    if (!ctx->efuse)
        return TRUE;

    /* Update MAC in eFuse at the correct offset for each chip type */
    switch (ctx->type) {
    case CHIP_ESP8266:
        /* ESP8266 MAC eFuse layout (esptool-js compatible):
           word0 (MAC_EFUSE_WORD0_ESP8266): NIC[2]=MAC[5] at byte[3]
           word1 (MAC_EFUSE_WORD1_ESP8266): NIC[0]=MAC[3] at byte[1], NIC[1]=MAC[4] at byte[0]
           word3 (MAC_EFUSE_WORD3_ESP8266): OUI source */
        ctx->efuse[0x53] = mac[5];  /* byte[3] of word0: (mac0 >> 24) = MAC[5] */
        ctx->efuse[0x54] = mac[4];  /* byte[0] of word1: mac1 & 0xFF = MAC[4] */
        ctx->efuse[0x55] = mac[3];  /* byte[1] of word1: (mac1 >> 8) = MAC[3] */
        /* word3 for custom OUI */
        ctx->efuse[0x5C] = mac[2];  /* byte[0] of word3: OUI byte 2 */
        ctx->efuse[0x5D] = mac[1];  /* byte[1] of word3: OUI byte 1 */
        ctx->efuse[0x5E] = mac[0];  /* byte[2] of word3: OUI byte 0 */
        break;
    case CHIP_ESP32:
        /* readEfuse(1)=MAC_EFUSE_WORD1_ESP32: mac[5],mac[4],mac[3],mac[2] */
        ctx->efuse[4] = mac[5];
        ctx->efuse[5] = mac[4];
        ctx->efuse[6] = mac[3];
        ctx->efuse[7] = mac[2];
        /* readEfuse(2)=MAC_EFUSE_WORD2_ESP32: mac[1],mac[0],0x00,0x00 */
        ctx->efuse[8] = mac[1];
        ctx->efuse[9] = mac[0];
        break;
    case CHIP_ESP32S2:
        /* MAC at EFUSE_BASE_ESP32S2 + 0x044 */
        ctx->efuse[0x44] = mac[5];
        ctx->efuse[0x45] = mac[4];
        ctx->efuse[0x46] = mac[3];
        ctx->efuse[0x47] = mac[2];
        ctx->efuse[0x48] = mac[1];
        ctx->efuse[0x49] = mac[0];
        break;
    case CHIP_ESP32C2:
        /* MAC at EFUSE_BASE_ESP32C2 + 0x040 */
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

/*
 * Chip_GetMac - Get MAC address
 *
 * Returns pointer to 6-byte MAC address array.
 */
const BYTE *Chip_GetMac(const CHIP_CTX *ctx)
{
    return ctx->mac;
}

/*
 * Chip_ReadReg - Read register value
 *
 * Simulates reading from various chip registers including:
 * - eFuse memory (multiple address ranges per chip type)
 * - Chip detection register (0x40001000)
 * - UART clock divider register (for crystal frequency detection)
 * - SPI registers
 *
 * @ctx:  Pointer to chip context (const, read-only)
 * @addr: Register address to read
 *
 * Returns 32-bit register value, or 0 for unmapped addresses.
 */
DWORD Chip_ReadReg(const CHIP_CTX *ctx, DWORD addr)
{
    /* ESP32 EFUSE: EFUSE_BASE_ESP32 + offset (ROM direct access) */
    if (addr >= EFUSE_BASE_ESP32 && addr < EFUSE_BASE_ESP32 + (DWORD)ctx->efuse_size) {
        int offset = (int)(addr - EFUSE_BASE_ESP32);
        if (offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] | 
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) | 
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32 EFUSE_RD_REG_BASE: esptool readEfuse */
    if (addr >= EFUSE_RD_REG_BASE_ESP32 && addr < EFUSE_RD_REG_BASE_ESP32 + 0x100) {
        int offset = (int)(addr - EFUSE_RD_REG_BASE_ESP32);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32 flash size register */
    if (addr == FLASH_SIZE_REG_ESP32)
        return (ctx->flash_size >> 16) & 0xFFFF;

    /* ESP32-S2 EFUSE */
    if (addr >= EFUSE_BASE_ESP32S2 && addr < EFUSE_BASE_ESP32S2 + 0x100) {
        int offset = (int)(addr - EFUSE_BASE_ESP32S2);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32-C2/C3 EFUSE */
    if (addr >= EFUSE_BASE_ESP32C2 && addr < EFUSE_BASE_ESP32C2 + 0x100) {
        int offset = (int)(addr - EFUSE_BASE_ESP32C2);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32-S3 EFUSE */
    if (addr >= EFUSE_BASE_ESP32S3 && addr < EFUSE_BASE_ESP32S3 + 0x100) {
        int offset = (int)(addr - EFUSE_BASE_ESP32S3);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* ESP32-C6 EFUSE */
    if (addr >= EFUSE_BASE_ESP32C6 && addr < EFUSE_BASE_ESP32C6 + 0x100) {
        int offset = (int)(addr - EFUSE_BASE_ESP32C6);
        if (ctx->efuse && offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] |
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) |
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    /* Chip detection magic register - used by esptool for autodetect */
    if (addr == CHIP_DETECT_REG)
        return ctx->chip_id;

    /* UART clock divider register - used for crystal frequency detection.
       esptool-js calculates: etsXtal = (baudrate * uartDiv) / 1000000 / XTAL_CLK_DIVIDER
       For ESP8266: XTAL_CLK_DIVIDER=2, so uartDiv = APB_CLK / baudrate = (2*xtal) / baudrate
       For other chips: XTAL_CLK_DIVIDER=1, so uartDiv = xtal / baudrate */
    if (addr == UART_CLKDIV_REG_ESP8266 || addr == UART_CLKDIV_REG_ESP32 || addr == UART_CLKDIV_REG_ESP32S2) {
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

/*
 * Chip_WriteReg - Write register value
 *
 * Simulates writing to various chip registers including:
 * - eFuse memory (uses OR operation to simulate OTP behavior)
 * - SPI registers (with simulated SPI command execution)
 *
 * eFuse write behavior: eFuse bits can only be set from 0 to 1,
 * never cleared back to 0. This simulates real one-time-programmable memory.
 *
 * SPI command simulation: When SPI_CMD_USR bit is set in SPI_CMD register,
 * the simulated SPI controller executes the command (currently supports
 * JEDEC Read ID command 0x9F).
 *
 * @ctx: Pointer to chip context
 * @addr: Register address to write
 * @val: 32-bit value to write
 *
 * Returns TRUE on success.
 */
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

    /* ESP32 EFUSE */
    EFUSE_WRITE_AT(EFUSE_BASE_ESP32)

    /* ESP32-S2 EFUSE */
    EFUSE_WRITE_AT(EFUSE_BASE_ESP32S2)

    /* ESP32-S3 EFUSE */
    EFUSE_WRITE_AT(EFUSE_BASE_ESP32S3)

    /* ESP32-C2/C3 EFUSE */
    EFUSE_WRITE_AT(EFUSE_BASE_ESP32C2)

    /* ESP32-C6 EFUSE */
    EFUSE_WRITE_AT(EFUSE_BASE_ESP32C6)

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

/*
 * Chip_SetFlashSize - Set flash size and update flash ID
 *
 * Updates the flash size and calculates the JEDEC flash ID based on
 * the new size. The flash ID format is 0xCCDDMM where:
 *   MM = Manufacturer ID (default: Winbond 0xEF)
 *   DD = Device ID high byte (0x40)
 *   CC = Capacity identifier (e.g. 0x16 for 4MB)
 *
 * @ctx:  Pointer to chip context
 * @size: Flash size in bytes (e.g. 4*1024*1024 for 4MB)
 */
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

/*
 * Chip_GetFlashSize - Get flash size in bytes
 */
DWORD Chip_GetFlashSize(const CHIP_CTX *ctx)
{
    return ctx->flash_size;
}

/*
 * Chip_GetChipId - Get chip ID register value
 *
 * Returns the chip ID used for autodetection (e.g. 0x00F01D83 for ESP32).
 */
DWORD Chip_GetChipId(const CHIP_CTX *ctx)
{
    return ctx->chip_id;
}

/*
 * Chip_GetEfuse - Get pointer to eFuse data
 *
 * Returns pointer to eFuse byte array, or NULL if not allocated.
 */
const BYTE *Chip_GetEfuse(const CHIP_CTX *ctx)
{
    return ctx->efuse;
}

/*
 * Chip_GetEfuseSize - Get eFuse size in bytes
 */
int Chip_GetEfuseSize(const CHIP_CTX *ctx)
{
    return ctx->efuse_size;
}

/*
 * Chip_GetBootBaudRate - Get boot message baud rate
 *
 * Returns the baud rate used for ROM bootloader boot messages.
 * ESP8266 and ESP32-C2 (26MHz XTAL) use 74880, others use 115200.
 */
DWORD Chip_GetBootBaudRate(const CHIP_CTX *ctx)
{
    if (ctx->type == CHIP_ESP8266)
        return 74880;
    if (ctx->type == CHIP_ESP32C2 && ctx->xtal_freq == XTAL_FREQ_26M)
        return 74880;
    return 115200;
}

/* Convert reset cause code to string */
static const char *ResetCauseStr(BYTE cause)
{
    switch (cause) {
    case 0x01: return "POWERON";
    case 0x02: return "EXT";
    case 0x03: return "WDT";
    default:   return "UNKNOWN";
    }
}

/*
 * Chip_GetBootMessage - Get ROM bootloader boot message
 *
 * Returns chip-specific boot message text that the ROM bootloader
 * outputs on UART after reset. The message format depends on whether
 * the chip enters download mode or performs a normal SPI flash boot.
 *
 * Download mode messages include "waiting for download".
 * Normal boot messages include SPI config and segment loading info.
 *
 * @ctx:           Pointer to chip context
 * @download_mode: TRUE for download mode entry, FALSE for normal flash boot
 * @reset_cause:   Reset cause code (0x01=POWERON, 0x02=EXT, 0x03=WDT)
 *
 * Returns pointer to static buffer containing multi-line ASCII string
 * with \r\n line endings. Buffer is valid until next call to this function.
 */
const char *Chip_GetBootMessage(const CHIP_CTX *ctx, BOOL download_mode, BYTE reset_cause)
{
    const char *rst = ResetCauseStr(reset_cause);
    static char buf[512];

    if (download_mode) {
        /* Download mode: ROM waits for UART sync from esptool */
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
    } else {
        /* Normal SPI flash boot: ROM loads firmware from flash */
        switch (ctx->type) {
        case CHIP_ESP8266:
            snprintf(buf, sizeof(buf),
                "ets Jan  8 2014,rst cause %d, boot mode:(3,7)\r\n"
                "\r\n"
                "load 0x40100000, len 24236, room 16 \r\n"
                "tail 12\r\n"
                "chksum 0xb7\r\n"
                "ho 0 tail 12 room 4\r\n"
                "load 0x3ffe8000, len 3008, room 12 \r\n"
                "tail 4\r\n"
                "chksum 0x2c\r\n"
                "load 0x3ffe8bc0, len 4816, room 4 \r\n"
                "tail 12\r\n"
                "chksum 0x46\r\n"
                "csum 0x46\r\n",
                reset_cause);
            break;
        case CHIP_ESP32:
            snprintf(buf, sizeof(buf),
                "ESP-ROM:esp32-20210719\r\n"
                "Build:Jul 19 2021\r\n"
                "rst:0x%02X (%s),boot:0x13 (SPI_FAST_FLASH_BOOT)\r\n"
                "configsip: 0, SPIWP:0x00\r\n"
                "clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00\r\n"
                "mode:DIO, clock div:1\r\n"
                "load:0x3fff0008,len:8\r\n"
                "load:0x3fff0010,len:3680\r\n"
                "load:0x40078000,len:8364\r\n"
                "load:0x40080000,len:252\r\n"
                "entry 0x40080034\r\n",
                reset_cause, rst);
            break;
        case CHIP_ESP32S2:
            snprintf(buf, sizeof(buf),
                "ESP-ROM:esp32s2-20210719\r\n"
                "Build:Jul 19 2021\r\n"
                "rst:0x%02X (%s),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
                "SPIWP:0xee\r\n"
                "mode:DIO, clock div:1\r\n"
                "load:0x3fff0008,len:8\r\n"
                "load:0x3fff0010,len:3680\r\n"
                "load:0x40078000,len:8364\r\n"
                "load:0x40080000,len:252\r\n"
                "entry 0x40080034\r\n",
                reset_cause, rst);
            break;
        case CHIP_ESP32S3:
            snprintf(buf, sizeof(buf),
                "ESP-ROM:esp32s3-20210719\r\n"
                "Build:Jul 19 2021\r\n"
                "rst:0x%02X (%s),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
                "SPIWP:0xee\r\n"
                "mode:DIO, clock div:1\r\n"
                "load:0x3fff0008,len:8\r\n"
                "load:0x3fff0010,len:3680\r\n"
                "load:0x40078000,len:8364\r\n"
                "load:0x40080000,len:252\r\n"
                "entry 0x40080034\r\n",
                reset_cause, rst);
            break;
        case CHIP_ESP32C2:
            snprintf(buf, sizeof(buf),
                "ESP-ROM:esp8684-api2-20220127\r\n"
                "Build:Jan 27 2022\r\n"
                "rst:0x%02X (%s),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
                "SPIWP:0xee\r\n"
                "mode:DIO, clock div:1\r\n"
                "load:0x3fff0008,len:8\r\n"
                "load:0x3fff0010,len:3680\r\n"
                "load:0x40078000,len:8364\r\n"
                "load:0x40080000,len:252\r\n"
                "entry 0x40080034\r\n",
                reset_cause, rst);
            break;
        case CHIP_ESP32C3:
            snprintf(buf, sizeof(buf),
                "ESP-ROM:esp32c3-20210719\r\n"
                "Build:Jul 19 2021\r\n"
                "rst:0x%02X (%s),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
                "SPIWP:0xee\r\n"
                "mode:DIO, clock div:1\r\n"
                "load:0x3fff0008,len:8\r\n"
                "load:0x3fff0010,len:3680\r\n"
                "load:0x40078000,len:8364\r\n"
                "load:0x40080000,len:252\r\n"
                "entry 0x40080034\r\n",
                reset_cause, rst);
            break;
        case CHIP_ESP32C6:
            snprintf(buf, sizeof(buf),
                "ESP-ROM:esp32c6-20210719\r\n"
                "Build:Jul 19 2021\r\n"
                "rst:0x%02X (%s),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
                "SPIWP:0xee\r\n"
                "mode:DIO, clock div:1\r\n"
                "load:0x3fff0008,len:8\r\n"
                "load:0x3fff0010,len:3680\r\n"
                "load:0x40078000,len:8364\r\n"
                "load:0x40080000,len:252\r\n"
                "entry 0x40080034\r\n",
                reset_cause, rst);
            break;
        default:
            buf[0] = '\0';
            break;
        }
    }

    return buf;
}
