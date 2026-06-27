/*
 * chip.c - ESP chip characteristics implementation
 *
 * Simulates chip properties, eFuse, and register access.
 */

#include "../fesptool_hal.h"
#include "chip_priv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if ENABLE_TRACE
static const char *TAG = "CHIP";
#endif

/* SPI register offsets for ESP32-S2/S3, ESP32-C2/C3/C6 (common layout) */
static const fesp_spi_offsets_t spi_offs_esp32s2 = {
    .usr = 0x18,       /* SPI_USR */
    .usr1 = 0x1C,      /* SPI_USR1 */
    .usr2 = 0x20,      /* SPI_USR2 */
    .w0 = 0x58,        /* SPI_W0 */
    .mosi_dlen = 0x24, /* SPI_MOSI_DLEN */
    .miso_dlen = 0x28, /* SPI_MISO_DLEN */
};

/* SPI register offsets for ESP32 */
static const fesp_spi_offsets_t spi_offs_esp32 = {
    .usr = 0x1C,       /* SPI_USR */
    .usr1 = 0x20,      /* SPI_USR1 */
    .usr2 = 0x24,      /* SPI_USR2 */
    .w0 = 0x80,        /* SPI_W0 */
    .mosi_dlen = 0x28, /* SPI_MOSI_DLEN */
    .miso_dlen = 0x2C, /* SPI_MISO_DLEN */
};

/* SPI register offsets for ESP8266 */
static const fesp_spi_offsets_t spi_offs_esp8266 = {
    .usr = 0x1C,       /* SPI_USR */
    .usr1 = 0x20,      /* SPI_USR1 */
    .usr2 = 0x24,      /* SPI_USR2 */
    .w0 = 0x40,        /* SPI_W0 */
    .mosi_dlen = 0x00, /* Not supported on ESP8266 */
    .miso_dlen = 0x00, /* Not supported on ESP8266 */
};

/* Chip configuration table */
typedef struct {
    const char *name;
    uint32_t chip_id;          /* Magic value for READ_REG detection */
    uint32_t security_chip_id; /* IMAGE_CHIP_ID for GET_SECURITY_INFO */
    int efuse_size;
    bool has_usb;
    uint8_t chip_id_bytes[4]; /* Chip ID as little-endian bytes for eFuse */
} CHIP_CONFIG;

static const CHIP_CONFIG chip_configs[FESP_CHIP_COUNT] = {
    [FESP_CHIP_ESP8266] = {"ESP8266",
                           FESP_CHIP_ID_ESP8266,
                           IMAGE_FESP_CHIP_ID_ESP8266,
                           96,
                           false,
                           {0x01, 0xC1, 0xF0, 0xFF}},
    [FESP_CHIP_ESP32] = {"ESP32",
                         FESP_CHIP_ID_ESP32,
                         IMAGE_FESP_CHIP_ID_ESP32,
                         288,
                         false,
                         {0x83, 0x1D, 0xF0, 0x00}},
    [FESP_CHIP_ESP32S2] = {"ESP32-S2",
                           FESP_CHIP_ID_ESP32S2,
                           IMAGE_FESP_CHIP_ID_ESP32S2,
                           512,
                           true,
                           {0xC6, 0x07, 0x00, 0x00}},
    [FESP_CHIP_ESP32S3] = {"ESP32-S3",
                           FESP_CHIP_ID_ESP32S3,
                           IMAGE_FESP_CHIP_ID_ESP32S3,
                           512,
                           true,
                           {0x09, 0x00, 0x00, 0x00}},
    [FESP_CHIP_ESP32C2] = {"ESP32-C2",
                           FESP_CHIP_ID_ESP32C2,
                           FESP_IMAGE_FESP_CHIP_ID_ESP32C2,
                           512,
                           false,
                           {0x6F, 0xA0, 0x41, 0x7C}},
    [FESP_CHIP_ESP32C3] = {"ESP32-C3",
                           FESP_CHIP_ID_ESP32C3,
                           IMAGE_FESP_CHIP_ID_ESP32C3,
                           512,
                           true,
                           {0x6F, 0x50, 0x21, 0x69}},
    [FESP_CHIP_ESP32C6] = {"ESP32-C6",
                           FESP_CHIP_ID_ESP32C6,
                           IMAGE_FESP_CHIP_ID_ESP32C6,
                           512,
                           true,
                           {0x6F, 0x80, 0xE0, 0x2C}},
};

/*
 * write_chip_id_to_efuse - Write chip_id as little-endian bytes at eFuse offset
 * 0x4C
 *
 * (where esptool reads EFUSE_RD_REG for chip detection)
 */
static void write_chip_id_to_efuse(fesp_chip_ctx_t *ctx)
{
    if (!ctx->efuse || ctx->efuse_size < 0x50) {
        return;
    }
    ctx->efuse[0x4C] = (uint8_t)(ctx->chip_id & 0xFF);
    ctx->efuse[0x4D] = (uint8_t)((ctx->chip_id >> 8) & 0xFF);
    ctx->efuse[0x4E] = (uint8_t)((ctx->chip_id >> 16) & 0xFF);
    ctx->efuse[0x4F] = (uint8_t)((ctx->chip_id >> 24) & 0xFF);
}

/*
 * init_chip_common - Common chip initialization
 */
static bool init_chip_common(fesp_chip_ctx_t *ctx, fesp_chip_type_t type)
{
    const CHIP_CONFIG *cfg = &chip_configs[type];

    strcpy(ctx->name, cfg->name);
    ctx->chip_id = cfg->chip_id;
    ctx->security_chip_id = cfg->security_chip_id;
    ctx->pkg_version = 0;
    ctx->efuse_size = cfg->efuse_size;
    ctx->efuse_conf_ofs = 0;
    ctx->efuse_cmd_ofs = 0;
    memset(ctx->pgm_data, 0, sizeof(ctx->pgm_data));
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = cfg->has_usb;

    ctx->efuse = (uint8_t *)fesp_hal_mem_zero_alloc(ctx->efuse_size);
    if (!ctx->efuse) {
        FESP_HAL_LOGD(TAG, "Failed to allocate eFuse for %s", cfg->name);
        return false;
    }

    /* Note: chip_id_bytes are NOT written here because for ESP32,
       efuse[0-3] is BLOCK0 word0 which contains WR_DIS.
       Each chip's init function handles chip_id placement if needed. */

    return true;
}

/*
 * write_mac_esp8266 - Write MAC address to eFuse for ESP8266
 *
 * ESP8266 eFuse layout (from esptool source):
 * - BLOCK0 (0x00-0x17): EFUSE_DATA0-3
 * - BLOCK1 (0x18-0x27): EFUSE_DATA4-7
 * - BLOCK2 (0x28-0x3B): EFUSE_DATA8-13
 * - BLOCK3 (0x3C-0x5F): EFUSE_DATA14-23 (MAC stored here)
 *
 * MAC eFuse offsets:
 * - word14 (0x50-0x53): MAC[5] at uint8_t[3], MAC[4] at uint8_t[2], ...
 * - word15 (0x54-0x57): MAC[3] at uint8_t[1], MAC[2] at uint8_t[0]
 * - word18 (0x5C-0x5F): Custom OUI (MAC[0-2])
 */
static void write_mac_esp8266(fesp_chip_ctx_t *ctx)
{
    ctx->efuse[0x53] =
        ctx->mac[5]; /* uint8_t[3] of word0: (mac0 >> 24) = MAC[5] */
    ctx->efuse[0x54] =
        ctx->mac[4]; /* uint8_t[0] of word1: mac1 & 0xFF = MAC[4] */
    ctx->efuse[0x55] =
        ctx->mac[3]; /* uint8_t[1] of word1: (mac1 >> 8) = MAC[3] */
    /* word3 for custom OUI: [mac[2], mac[1], mac[0]] in little-endian */
    ctx->efuse[0x5C] = ctx->mac[2]; /* uint8_t[0] of word3: OUI uint8_t 2 */
    ctx->efuse[0x5D] = ctx->mac[1]; /* uint8_t[1] of word3: OUI uint8_t 1 */
    ctx->efuse[0x5E] = ctx->mac[0]; /* uint8_t[2] of word3: OUI uint8_t 0 */
}

/*
 * write_mac_esp32 - Write MAC address to eFuse for ESP32
 *
 * ESP32 eFuse layout (from TRM Table 28-5):
 * - eFuse word0 (0x00-0x03): chip_id
 * - eFuse word1 (0x04-0x07): MAC[5:2]
 * - eFuse word2 (0x08-0x0B): MAC[1:0] + padding
 *
 * MAC eFuse read base: 0x3FF5A000 (FESP_EFUSE_RD_REG_BASE_ESP32)
 */
static void write_mac_esp32(fesp_chip_ctx_t *ctx)
{
    ctx->efuse[4] = ctx->mac[5];
    ctx->efuse[5] = ctx->mac[4];
    ctx->efuse[6] = ctx->mac[3];
    ctx->efuse[7] = ctx->mac[2];
    /* MAC at EFUSE_RD_REG_BASE + 8 (word2): mac[1],mac[0],0x00,0x00 */
    ctx->efuse[8] = ctx->mac[1];
    ctx->efuse[9] = ctx->mac[0];
}

/*
 * write_mac_at_0x44 - Write MAC address to eFuse for ESP32-S2/S3/C3/C6
 *
 * Common eFuse layout (from respective TRMs):
 * - eFuse word17 (offset 0x44-0x47): MAC[5:2]
 * - eFuse word18 (offset 0x48-0x4B): MAC[1:0] + padding
 *
 * Base addresses:
 * - ESP32-S2: 0x3F41A000 (FESP_EFUSE_BASE_ESP32S2)
 * - ESP32-S3: 0x60007000 (FESP_EFUSE_BASE_ESP32S3)
 * - ESP32-C3: 0x60008800 (FESP_EFUSE_BASE_ESP32C3)
 * - ESP32-C6: 0x600B0800 (FESP_EFUSE_BASE_ESP32C6)
 */
static void write_mac_at_0x44(fesp_chip_ctx_t *ctx)
{
    ctx->efuse[0x44] = ctx->mac[5];
    ctx->efuse[0x45] = ctx->mac[4];
    ctx->efuse[0x46] = ctx->mac[3];
    ctx->efuse[0x47] = ctx->mac[2];
    ctx->efuse[0x48] = ctx->mac[1];
    ctx->efuse[0x49] = ctx->mac[0];
}

/*
 * write_mac_at_0x40 - Write MAC address to eFuse for ESP32-C2
 *
 * ESP32-C2 eFuse layout (from TRM):
 * - eFuse word16 (offset 0x40-0x43): MAC[5:2]
 * - eFuse word17 (offset 0x44-0x47): MAC[1:0] + padding
 *
 * Note: ESP32-C2 uses offset 0x40 (not 0x44 like other chips)
 * Base address: 0x60008800 (FESP_EFUSE_BASE_ESP32C2)
 */
static void write_mac_at_0x40(fesp_chip_ctx_t *ctx)
{
    ctx->efuse[0x40] = ctx->mac[5];
    ctx->efuse[0x41] = ctx->mac[4];
    ctx->efuse[0x42] = ctx->mac[3];
    ctx->efuse[0x43] = ctx->mac[2];
    ctx->efuse[0x44] = ctx->mac[1];
    ctx->efuse[0x45] = ctx->mac[0];
}

/*
 * init_esp8266 - Initialize ESP8266 chip context
 */
static bool init_esp8266(fesp_chip_ctx_t *ctx)
{
    if (!init_chip_common(ctx, FESP_CHIP_ESP8266)) {
        return false;
    }
    write_mac_esp8266(ctx);
    ctx->efuse_base = 0x3FF00000; /* ESP8266 eFuse register base */
    return true;
}

/*
 * init_esp32 - Initialize ESP32 chip context
 */
static bool init_esp32(fesp_chip_ctx_t *ctx)
{
    if (!init_chip_common(ctx, FESP_CHIP_ESP32)) {
        return false;
    }

    /* Set BLOCK0 default values matching real ESP32 hardware.
       BLOCK0 is at offset 0x00 in the eFuse array (EFUSE_RD_REG_BASE mapping).
       Word0 (offset 0x00): CHIP_CPU_FREQ_RATED(bit31) +
       CONSOLE_DEBUG_DISABLE(bit12) Word5 (offset 0x14): FLASH_CRYPT_CONFIG
       default = 0xF (all tweak bits) */
    ctx->efuse[0x00] = 0x00;
    ctx->efuse[0x01] = 0x10;
    ctx->efuse[0x02] = 0x00;
    ctx->efuse[0x03] = 0x80; /* word0 = 0x80001000 */
    ctx->efuse[0x17] = 0xF0; /* FLASH_CRYPT_CONFIG = 0xF at word5 bits[31:28] */
    FESP_HAL_LOGD(TAG,
                  "init_esp32: BLOCK0 defaults set, word0=%02X%02X%02X%02X, "
                  "crypt_cfg=%02X, efuse=%p",
                  ctx->efuse[0x03], ctx->efuse[0x02], ctx->efuse[0x01],
                  ctx->efuse[0x00], ctx->efuse[0x14], ctx->efuse);

    write_mac_esp32(ctx);
    FESP_HAL_LOGD(TAG, "init_esp32: After MAC write, word0=%02X%02X%02X%02X",
                  ctx->efuse[0x03], ctx->efuse[0x02], ctx->efuse[0x01],
                  ctx->efuse[0x00]);
    /* ESP32 chip detection uses magic value at 0x40001000
       (FESP_CHIP_DETECT_REG), not eFuse. Do NOT call write_chip_id_to_efuse as
       it would overlap with BLOCK1 (key area at 0x38-0x57). */

    /* eFuse controller register offsets (from EFUSE_RD_REG_BASE 0x3FF5A000) */
    ctx->efuse_base = FESP_EFUSE_RD_REG_BASE_ESP32;
    ctx->efuse_conf_ofs = 0x0FC; /* EFUSE_REG_CONF */
    ctx->efuse_cmd_ofs = 0x104;  /* EFUSE_REG_CMD */

    return true;
}

/*
 * init_esp32s2 - Initialize ESP32-S2 chip context
 */
static bool init_esp32s2(fesp_chip_ctx_t *ctx)
{
    if (!init_chip_common(ctx, FESP_CHIP_ESP32S2)) {
        return false;
    }
    write_mac_at_0x44(ctx);
    write_chip_id_to_efuse(ctx);

    /* Set chip revision to 1.0 in eFuse.
       ESP32-S2: EFUSE_BLOCK1_ADDR = 0x3F41A044
       major at word3 (BLOCK1 + 12) bits[19:18] = uint8_t 0x51 bits[3:2] */
    ctx->efuse[0x51] |= 0x04; /* major=1, bits[19:18] = 01 */

    /* eFuse controller register offsets (from EFUSE_BASE 0x3F41A000) */
    ctx->efuse_base = FESP_EFUSE_BASE_ESP32S2;
    ctx->efuse_conf_ofs = 0x1CC; /* EFUSE_CONF_REG */
    ctx->efuse_cmd_ofs = 0x1D4;  /* EFUSE_CMD_REG */

    return true;
}

/*
 * init_esp32s3 - Initialize ESP32-S3 chip context
 */
static bool init_esp32s3(fesp_chip_ctx_t *ctx)
{
    if (!init_chip_common(ctx, FESP_CHIP_ESP32S3)) {
        return false;
    }
    write_mac_at_0x44(ctx);
    write_chip_id_to_efuse(ctx);

    /* Set chip revision to v0.0 in eFuse via ECO0 detection workaround.
       ESP32-S3's is_eco0() checks:
         (minor_raw & 0x7) == 0 AND blk_version_major == 1 AND blk_version_minor
       == 1 When is_eco0() returns true, get_major_chip_version() returns 0.

       blk_version_major at BLOCK2 word4 bits[1:0] (EFUSE_BLOCK2_ADDR + 16)
         = EFUSE_BASE + 0x5C + 0x10 = offset 0x6C, uint8_t 0x6C bits[1:0]
       blk_version_minor at BLOCK1 word3 bits[26:24] (EFUSE_BLOCK1_ADDR + 12)
         = EFUSE_BASE + 0x44 + 0x0C = offset 0x50, uint8_t 0x52 bits[2:0] */
    ctx->efuse[0x6C] |= 0x01; /* blk_version_major = 1 */
    ctx->efuse[0x52] |= 0x01; /* blk_version_minor = 1 */

    /* eFuse controller register offsets (from EFUSE_BASE 0x60007000) */
    ctx->efuse_base = FESP_EFUSE_BASE_ESP32S3;
    ctx->efuse_conf_ofs = 0x1CC; /* EFUSE_CONF_REG */
    ctx->efuse_cmd_ofs = 0x1D4;  /* EFUSE_CMD_REG */

    return true;
}

/*
 * init_esp32c2 - Initialize ESP32-C2 chip context
 */
static bool init_esp32c2(fesp_chip_ctx_t *ctx)
{
    if (!init_chip_common(ctx, FESP_CHIP_ESP32C2)) {
        return false;
    }
    write_mac_at_0x40(ctx);
    write_chip_id_to_efuse(ctx);

    /* Set chip revision to 1.0 (major=1, minor=0) in eFuse.
       ESP32-C2 reads revision from EFUSE_BLOCK2_ADDR + 4 (offset 0x44):
         major = bits[21:20] = uint8_t 0x46 bits[5:4]
         minor = bits[19:16] = uint8_t 0x46 bits[3:0]
       Revision 0 (ECO0) causes esptool to disable the stub flasher. */
    ctx->efuse[0x46] |= 0x10; /* major=1, minor=0 */

    /* eFuse controller register offsets (from EFUSE_BASE 0x60008800) */
    ctx->efuse_base = FESP_EFUSE_BASE_ESP32C2;
    ctx->efuse_conf_ofs = 0x8C; /* EFUSE_CONF_REG */
    ctx->efuse_cmd_ofs = 0x94;  /* EFUSE_CMD_REG */

    return true;
}

/*
 * init_esp32c3 - Initialize ESP32-C3 chip context
 */
static bool init_esp32c3(fesp_chip_ctx_t *ctx)
{
    if (!init_chip_common(ctx, FESP_CHIP_ESP32C3)) {
        return false;
    }
    write_mac_at_0x44(ctx);
    write_chip_id_to_efuse(ctx);

    /* Set chip revision to 1.0 in eFuse.
       ESP32-C3: EFUSE_BLOCK1_ADDR = 0x60008844
       major at word5 (BLOCK1 + 20) bits[25:24] = uint8_t 0x5B bits[1:0] */
    ctx->efuse[0x5B] |= 0x01; /* major=1, bits[25:24] = 01 */

    /* eFuse controller register offsets (from EFUSE_BASE 0x60008800) */
    ctx->efuse_base = FESP_EFUSE_BASE_ESP32C3;
    ctx->efuse_conf_ofs = 0x1CC; /* EFUSE_CONF_REG */
    ctx->efuse_cmd_ofs = 0x1D4;  /* EFUSE_CMD_REG */

    return true;
}

/*
 * init_esp32c6 - Initialize ESP32-C6 chip context
 */
static bool init_esp32c6(fesp_chip_ctx_t *ctx)
{
    if (!init_chip_common(ctx, FESP_CHIP_ESP32C6)) {
        return false;
    }
    write_mac_at_0x44(ctx);
    write_chip_id_to_efuse(ctx);

    /* ESP32-C6: no chip revision override needed.
       Leave eFuse at 0 -> major=0, minor=0 -> v0.0 */

    /* eFuse controller register offsets (from EFUSE_BASE 0x600B0800) */
    ctx->efuse_base = FESP_EFUSE_BASE_ESP32C6;
    ctx->efuse_conf_ofs = 0x1CC; /* EFUSE_CONF_REG (same as C3) */
    ctx->efuse_cmd_ofs = 0x1D4;  /* EFUSE_CMD_REG (same as C3) */

    return true;
}

/*
 * fesp_chip_init - Initialize chip context with type-specific defaults
 *
 * Sets up chip properties, allocates eFuse memory, and configures
 * SPI register offsets based on chip type.
 *
 * @ctx:  Pointer to chip context to initialize
 * @type: Chip type enum (FESP_CHIP_ESP8266, FESP_CHIP_ESP32, etc.)
 *
 * Returns true on success, false on failure (memory allocation error).
 * On failure, any allocated resources are automatically freed.
 */
bool fesp_chip_init(fesp_chip_ctx_t *ctx, fesp_chip_type_t type)
{
    memset(ctx, 0, sizeof(fesp_chip_ctx_t));
    ctx->type = type;
    ctx->xtal_freq = FESP_XTAL_FREQ_40M;

    ctx->mac[0] = 0xAA;
    ctx->mac[1] = 0xBB;
    ctx->mac[2] = 0xCC;
    ctx->mac[3] = 0xDD;
    ctx->mac[4] = 0xEE;
    ctx->mac[5] = 0x01;

    /* Set SPI register base address and offsets by chip type */
    switch (type) {
    case FESP_CHIP_ESP8266:
        ctx->spi_reg_base = FESP_SPI_REG_BASE_ESP8266;
        ctx->spi_offs = &spi_offs_esp8266;
        break;
    case FESP_CHIP_ESP32:
        ctx->spi_reg_base = FESP_SPI_REG_BASE_ESP32;
        ctx->spi_offs = &spi_offs_esp32;
        break;
    case FESP_CHIP_ESP32S2:
        ctx->spi_reg_base = FESP_SPI_REG_BASE_ESP32S2;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    case FESP_CHIP_ESP32C6:
        ctx->spi_reg_base = FESP_SPI_REG_BASE_ESP32C6;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    default:
        /* S3, C2, C3 */
        ctx->spi_reg_base = FESP_SPI_REG_BASE_ESP32S3;
        ctx->spi_offs = &spi_offs_esp32s2;
        break;
    }

    switch (type) {
    case FESP_CHIP_ESP8266:
        if (!init_esp8266(ctx)) {
            goto fail;
        }
        break;
    case FESP_CHIP_ESP32:
        if (!init_esp32(ctx)) {
            goto fail;
        }
        break;
    case FESP_CHIP_ESP32S2:
        if (!init_esp32s2(ctx)) {
            goto fail;
        }
        break;
    case FESP_CHIP_ESP32S3:
        if (!init_esp32s3(ctx)) {
            goto fail;
        }
        break;
    case FESP_CHIP_ESP32C2:
        if (!init_esp32c2(ctx)) {
            goto fail;
        }
        break;
    case FESP_CHIP_ESP32C3:
        if (!init_esp32c3(ctx)) {
            goto fail;
        }
        break;
    case FESP_CHIP_ESP32C6:
        if (!init_esp32c6(ctx)) {
            goto fail;
        }
        break;
    default:
        FESP_HAL_LOGD(TAG, "Unknown chip type: %d", type);
        return false;
    }

    /* Set default flash size and ID */
    fesp_chip_set_flash_size(ctx, 4 * 1024 * 1024);

    /* Initialize SPI register defaults */
    ctx->spi_regs[FESP_SPI_CMD_OFFS / 4] = 0;

    FESP_HAL_LOGD(
        TAG,
        "Chip: %s, eFuse: %d bytes, Flash: %lu KB, SPI_BASE: 0x%08lX, "
        "SPI_W0: 0x%02X",
        ctx->name, ctx->efuse_size, ctx->flash_size / 1024, ctx->spi_reg_base,
        ctx->spi_offs->w0);
    return true;

fail:
    fesp_chip_close(ctx);
    return false;
}

/*
 * fesp_chip_close - Release chip resources
 *
 * Frees dynamically allocated eFuse memory.
 * Safe to call multiple times.
 */
void fesp_chip_close(fesp_chip_ctx_t *ctx)
{
    if (ctx->efuse) {
        fesp_hal_mem_free(ctx->efuse);
        ctx->efuse = NULL;
    }
}

/*
 * fesp_chip_get_name - Get chip name string
 *
 * Returns pointer to static chip name (e.g. "ESP32", "ESP32-C3").
 */
const char *fesp_chip_get_name(const fesp_chip_ctx_t *ctx) { return ctx->name; }

/*
 * fesp_chip_set_mac - Set MAC address and update eFuse
 *
 * Updates the MAC address in the chip context and writes it to the
 * correct eFuse offset based on chip type.
 *
 * @ctx: Pointer to chip context
 * @mac: 6-uint8_t MAC address array
 *
 * Returns true on success.
 */
bool fesp_chip_set_mac(fesp_chip_ctx_t *ctx, const uint8_t mac[6])
{
    memcpy(ctx->mac, mac, 6);

    if (!ctx->efuse) {
        return true;
    }

    /* Update MAC in eFuse at the correct offset for each chip type */
    switch (ctx->type) {
    case FESP_CHIP_ESP8266:
        write_mac_esp8266(ctx);
        break;
    case FESP_CHIP_ESP32:
        write_mac_esp32(ctx);
        break;
    case FESP_CHIP_ESP32S2:
    case FESP_CHIP_ESP32S3:
    case FESP_CHIP_ESP32C3:
    case FESP_CHIP_ESP32C6:
        write_mac_at_0x44(ctx);
        break;
    case FESP_CHIP_ESP32C2:
        write_mac_at_0x40(ctx);
        break;
    default:
        break;
    }

    return true;
}

/*
 * fesp_chip_get_mac - Get MAC address
 *
 * Returns pointer to 6-uint8_t MAC address array.
 */
const uint8_t *fesp_chip_get_mac(const fesp_chip_ctx_t *ctx)
{
    return ctx->mac;
}

/*
 * fesp_chip_read_reg - Read register value
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

/*
 * try_read_efuse32 - Try to read a 32-bit value from eFuse address range
 *
 * @ctx:    Pointer to chip context (const, read-only)
 * @base:   Base address of eFuse range
 * @size:   Size of eFuse range (0x100 for most chips, ctx->efuse_size for
 * ESP32)
 * @addr:   Register address to read
 * @result: Pointer to receive 32-bit value (set on success only)
 *
 * Returns true if address is in range and read succeeds, false otherwise.
 */
static bool try_read_efuse32(const fesp_chip_ctx_t *ctx, uint32_t base,
                             uint32_t size, uint32_t addr, uint32_t *result)
{
    if (addr < base || addr >= base + size) {
        return false;
    }
    int offset = (int)(addr - base);
    if (!ctx->efuse || offset + 3 >= ctx->efuse_size) {
        return false;
    }
    *result = ctx->efuse[offset] | ((uint32_t)ctx->efuse[offset + 1] << 8) |
              ((uint32_t)ctx->efuse[offset + 2] << 16) |
              ((uint32_t)ctx->efuse[offset + 3] << 24);
    return true;
}

uint32_t fesp_chip_read_reg(const fesp_chip_ctx_t *ctx, uint32_t addr)
{
    uint32_t val;

    /* 1. eFuse read (using cached base address) */
    if (ctx->efuse_base != 0) {
        if (try_read_efuse32(ctx, ctx->efuse_base, (uint32_t)ctx->efuse_size,
                             addr, &val))
            return val;
        /* ESP32 special: EFUSE_BASE (0x3FF00000) is also valid access path */
        if (ctx->type == FESP_CHIP_ESP32) {
            if (try_read_efuse32(ctx, FESP_EFUSE_BASE_ESP32,
                                 (uint32_t)ctx->efuse_size, addr, &val))
                return val;
        }
    }

    /* 2. Special registers */
    if (addr == FESP_CHIP_DETECT_REG) {
        return ctx->chip_id;
    }

    if (addr == FESP_FLASH_SIZE_REG_ESP32) {
        return (ctx->flash_size >> 16) & 0xFFFF;
    }

    /* 3. eFuse CMD_REG (poll for completion - always return 0) */
    if (ctx->efuse_cmd_ofs != 0 && ctx->efuse_base != 0 &&
        addr == ctx->efuse_base + ctx->efuse_cmd_ofs) {
        return 0;
    }

    /* 4. UART clock divider register */
    if (addr == FESP_UART_CLKDIV_REG_ESP8266 ||
        addr == FESP_UART_CLKDIV_REG_ESP32 ||
        addr == FESP_UART_CLKDIV_REG_ESP32S2) {
        uint32_t xtal;
        if (ctx->type == FESP_CHIP_ESP32C3 || ctx->type == FESP_CHIP_ESP32C6 ||
            ctx->type == FESP_CHIP_ESP32S2 || ctx->type == FESP_CHIP_ESP32S3) {
            xtal = 40000000;
        } else {
            switch (ctx->xtal_freq) {
            case FESP_XTAL_FREQ_26M:
                xtal = 26000000;
                break;
            default:
                xtal = 40000000;
                break;
            }
        }
        if (ctx->type == FESP_CHIP_ESP8266) {
            return (2 * xtal) / 115200;
        } else
            return xtal / 115200;
    }

    /* 5. SPI register read */
    if (ctx->spi_reg_base != 0 && addr >= ctx->spi_reg_base &&
        addr < ctx->spi_reg_base + FESP_SPI_REG_COUNT * 4) {
        return ctx->spi_regs[(addr - ctx->spi_reg_base) / 4];
    }

    return 0;
}

/*
 * fesp_chip_write_reg - Write register value
 *
 * Simulates writing to various chip registers including:
 * - eFuse memory (uses OR operation to simulate OTP behavior)
 * - SPI registers (with simulated SPI command execution)
 *
 * eFuse write behavior: eFuse bits can only be set from 0 to 1,
 * never cleared back to 0. This simulates real one-time-programmable memory.
 *
 * SPI command simulation: When FESP_SPI_CMD_USR bit is set in SPI_CMD register,
 * the simulated SPI controller executes the command (currently supports
 * JEDEC Read ID command 0x9F).
 *
 * @ctx: Pointer to chip context
 * @addr: Register address to write
 * @val: 32-bit value to write
 *
 * Returns true on success.
 */
bool fesp_chip_write_reg(fesp_chip_ctx_t *ctx, uint32_t addr, uint32_t val)
{
    /* 1. eFuse controller write (EFUSE_RD_REG_BASE range) */
    if (ctx->efuse_base != 0 && ctx->efuse_conf_ofs != 0 &&
        addr >= ctx->efuse_base && addr < ctx->efuse_base + ctx->efuse_size) {
        int offset = (int)(addr - ctx->efuse_base);

        if (ctx->type == FESP_CHIP_ESP32) {
            return fesp_chip_write_reg_esp32(ctx, offset, val);
        } else
            return fesp_chip_write_reg_modern(ctx, offset, val);
    }

    /* 1b. ESP32: eFuse controller writes at EFUSE_BASE (0x3FF42000) range.
       espefuse sends PGM_DATA and CMD_REG writes to this address range.
       Translate to EFUSE_RD_REG_BASE offset (-0x10) for
       fesp_chip_write_reg_esp32. Must be checked BEFORE SPI register handler
       (same address range). */
    if (ctx->type == FESP_CHIP_ESP32 && ctx->efuse_conf_ofs != 0 &&
        addr >= FESP_SPI_REG_BASE_ESP32 &&
        addr < FESP_SPI_REG_BASE_ESP32 + 0x100) {
        int offset = (int)(addr - FESP_SPI_REG_BASE_ESP32) - 0x10;
        return fesp_chip_write_reg_esp32(ctx, offset, val);
    }

    /* 2. SPI register write */
    if (ctx->spi_reg_base != 0 && addr >= ctx->spi_reg_base &&
        addr < ctx->spi_reg_base + FESP_SPI_REG_COUNT * 4) {
        int offset = (int)(addr - ctx->spi_reg_base);
        int idx = offset / 4;
        if (idx >= 0 && idx < FESP_SPI_REG_COUNT) {
            ctx->spi_regs[idx] = val;

            if (offset == FESP_SPI_CMD_OFFS && (val & FESP_SPI_CMD_USR)) {
                uint32_t usr2 = ctx->spi_regs[ctx->spi_offs->usr2 / 4];
                uint32_t cmd = usr2 & 0xFFFF;

                if (cmd == FESP_SPIFLASH_RDID) {
                    ctx->spi_regs[ctx->spi_offs->w0 / 4] = ctx->flash_id;
                    FESP_HAL_LOGD(TAG, "SPI RDID: flash_id=0x%08lX",
                                  ctx->flash_id);
                }

                ctx->spi_regs[FESP_SPI_CMD_OFFS / 4] &= ~FESP_SPI_CMD_USR;
            }
        }
        return true;
    }

    return true;
}

/*
 * fesp_chip_set_flash_size - Set flash size and update flash ID
 *
 * Updates the flash size and calculates the JEDEC flash ID based on
 * the new size. The flash ID format is 0xCCDDMM where:
 *   MM = Manufacturer ID (default: Winbond 0xEF)
 *   DD = Device ID high uint8_t (0x40)
 *   CC = Capacity identifier (e.g. 0x16 for 4MB)
 *
 * @ctx:  Pointer to chip context
 * @size: Flash size in bytes (e.g. 4*1024*1024 for 4MB)
 */
void fesp_chip_set_flash_size(fesp_chip_ctx_t *ctx, uint32_t size)
{
    ctx->flash_size = size;

    /* Update flash_id capacity uint8_t based on size.
       Flash ID format: 0xCCDDMM
         MM = Manufacturer ID
         DD = Device ID high uint8_t (0x40)
         CC = Capacity identifier */
    uint8_t cap_id;
    switch (size) {
    case 256 * 1024:
        cap_id = 0x12;
        break;
    case 512 * 1024:
        cap_id = 0x13;
        break;
    case 1 * 1024 * 1024:
        cap_id = 0x14;
        break;
    case 2 * 1024 * 1024:
        cap_id = 0x15;
        break;
    case 4 * 1024 * 1024:
        cap_id = 0x16;
        break;
    case 8 * 1024 * 1024:
        cap_id = 0x17;
        break;
    case 16 * 1024 * 1024:
        cap_id = 0x18;
        break;
    case 32 * 1024 * 1024:
        cap_id = 0x19;
        break;
    default:
        cap_id = 0x16;
        break; /* Default 4MB */
    }

    /* Default manufacturer: Winbond (0xEF)
       Other known manufacturers:
         0xEF = Winbond
         0xC8 = GigaDevice
         0x20 = XMC
         0xC2 = Macronix
         0xE0 = Espressif */
    ctx->flash_id = ((uint32_t)cap_id << 16) | 0x40EF;
}

/*
 * fesp_chip_get_flash_size - Get flash size in bytes
 */
uint32_t fesp_chip_get_flash_size(const fesp_chip_ctx_t *ctx)
{
    return ctx->flash_size;
}

/*
 * fesp_chip_get_chip_id - Get chip ID register value
 *
 * Returns the chip ID used for autodetection (e.g. 0x00F01D83 for ESP32).
 */
uint32_t fesp_chip_get_chip_id(const fesp_chip_ctx_t *ctx)
{
    return ctx->chip_id;
}

/*
 * fesp_chip_get_efuse - Get pointer to eFuse data
 *
 * Returns pointer to eFuse uint8_t array, or NULL if not allocated.
 */
const uint8_t *fesp_chip_get_efuse(const fesp_chip_ctx_t *ctx)
{
    return ctx->efuse;
}

/*
 * fesp_chip_get_efuse_mut - Get mutable pointer to eFuse data
 */
uint8_t *fesp_chip_get_efuse_mut(fesp_chip_ctx_t *ctx) { return ctx->efuse; }

/*
 * fesp_chip_get_efuse_size - Get eFuse size in bytes
 */
int fesp_chip_get_efuse_size(const fesp_chip_ctx_t *ctx)
{
    return ctx->efuse_size;
}

/*
 * fesp_chip_get_boot_baud_rate - Get boot message baud rate
 *
 * Returns the baud rate used for ROM bootloader boot messages.
 * ESP8266 and ESP32-C2 (26MHz XTAL) use 74880, others use 115200.
 */
uint32_t fesp_chip_get_boot_baud_rate(const fesp_chip_ctx_t *ctx)
{
    if (ctx->type == FESP_CHIP_ESP8266) {
        return 74880;
    }
    if (ctx->type == FESP_CHIP_ESP32C2 &&
        ctx->xtal_freq == FESP_XTAL_FREQ_26M) {
        return 74880;
    }
    return 115200;
}

/* Convert reset cause code to string */
static const char *ResetCauseStr(uint8_t cause)
{
    switch (cause) {
    case 0x01:
        return "POWERON";
    case 0x02:
        return "EXT";
    case 0x03:
        return "WDT";
    default:
        return "UNKNOWN";
    }
}

/*
 * fesp_chip_get_boot_message - Get ROM bootloader boot message
 *
 * Writes chip-specific boot message text to caller-provided buffer.
 * The ROM bootloader outputs this on UART after reset.
 * The message format depends on whether the chip enters download mode
 * or performs a normal SPI flash boot.
 *
 * Download mode messages include "waiting for download".
 * Normal boot messages include SPI config and segment loading info.
 *
 * @ctx:           Pointer to chip context
 * @download_mode: true for download mode entry, false for normal flash boot
 * @reset_cause:   Reset cause code (0x01=POWERON, 0x02=EXT, 0x03=WDT)
 * @buf:           Output buffer for message
 * @buf_size:      Size of output buffer
 *
 * Returns pointer to buf containing multi-line ASCII string
 * with \r\n line endings, or empty string if buffer is too small.
 */
const char *fesp_chip_get_boot_message(const fesp_chip_ctx_t *ctx,
                                       bool download_mode, uint8_t reset_cause,
                                       char *buf, size_t buf_size)
{
    const char *rst = ResetCauseStr(reset_cause);

    if (download_mode) {
        /* Download mode: ROM waits for UART sync from esptool */
        switch (ctx->type) {
        case FESP_CHIP_ESP8266:
            snprintf(buf, buf_size,
                     "ets_main.c 542 \r\n"
                     "ets_main.c 543 \r\n"
                     "rst:0x%02X (%s),boot:0x3 (DOWNLOAD(UART0/1/2))\r\n"
                     "waiting for download\r\n",
                     reset_cause, rst);
            break;
        case FESP_CHIP_ESP32:
            snprintf(buf, buf_size,
                     "ESP-ROM:esp32-20210719\r\n"
                     "Build:Jul 19 2021\r\n"
                     "rst:0x%02X (%s),boot:0x3 (DOWNLOAD(UART0/1/2))\r\n"
                     "waiting for download\r\n",
                     reset_cause, rst);
            break;
        case FESP_CHIP_ESP32S2:
            snprintf(buf, buf_size,
                     "ESP-ROM:esp32s2-20210719\r\n"
                     "Build:Jul 19 2021\r\n"
                     "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
                     "waiting for download\r\n",
                     reset_cause, rst);
            break;
        case FESP_CHIP_ESP32S3:
            snprintf(buf, buf_size,
                     "ESP-ROM:esp32s3-20210719\r\n"
                     "Build:Jul 19 2021\r\n"
                     "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
                     "waiting for download\r\n",
                     reset_cause, rst);
            break;
        case FESP_CHIP_ESP32C2:
            snprintf(buf, buf_size,
                     "ESP-ROM:esp8684-api2-20220127\r\n"
                     "Build:Jan 27 2022\r\n"
                     "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
                     "waiting for download\r\n",
                     reset_cause, rst);
            break;
        case FESP_CHIP_ESP32C3:
            snprintf(buf, buf_size,
                     "ESP-ROM:esp32c3-20210719\r\n"
                     "Build:Jul 19 2021\r\n"
                     "rst:0x%02X (%s),boot:0x4 (DOWNLOAD(UART0))\r\n"
                     "waiting for download\r\n",
                     reset_cause, rst);
            break;
        case FESP_CHIP_ESP32C6:
            snprintf(buf, buf_size,
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
        case FESP_CHIP_ESP8266:
            snprintf(buf, buf_size,
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
        case FESP_CHIP_ESP32:
            snprintf(buf, buf_size,
                     "ESP-ROM:esp32-20210719\r\n"
                     "Build:Jul 19 2021\r\n"
                     "rst:0x%02X (%s),boot:0x13 (SPI_FAST_FLASH_BOOT)\r\n"
                     "configsip: 0, SPIWP:0x00\r\n"
                     "clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:"
                     "0x00,wp_drv:0x00\r\n"
                     "mode:DIO, clock div:1\r\n"
                     "load:0x3fff0008,len:8\r\n"
                     "load:0x3fff0010,len:3680\r\n"
                     "load:0x40078000,len:8364\r\n"
                     "load:0x40080000,len:252\r\n"
                     "entry 0x40080034\r\n",
                     reset_cause, rst);
            break;
        case FESP_CHIP_ESP32S2:
            snprintf(buf, buf_size,
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
        case FESP_CHIP_ESP32S3:
            snprintf(buf, buf_size,
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
        case FESP_CHIP_ESP32C2:
            snprintf(buf, buf_size,
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
        case FESP_CHIP_ESP32C3:
            snprintf(buf, buf_size,
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
        case FESP_CHIP_ESP32C6:
            snprintf(buf, buf_size,
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
