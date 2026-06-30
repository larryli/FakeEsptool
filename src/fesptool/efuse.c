/*
 * efuse.c - eFuse controller simulation and field queries for ESP chips.
 */

#include "../fesptool_hal.h"
#include "efuse_priv.h"
#include <stdio.h>
#include <string.h>

#if FESP_HAL_LOG_HAS_DEBUG
static const char *TAG = "EFUSE";
#endif

/* ESP32-C3 eFuse block read-back offsets (from DR_REG_EFUSE_BASE 0x60008800)
 * Source: espefuse/efuse/esp32c3/mem_definition.py __base_rd_regs offsets */
static const uint32_t efuse_block_offsets_c3[] = {
    0x02C, /* BLOCK0 (6 words) */
    0x044, /* BLOCK1/MAC (6 words) */
    0x05C, /* BLOCK2/SYS_DATA (8 words) */
    0x07C, /* BLOCK3/USR_DATA (8 words) */
    0x09C, /* BLOCK_KEY0 (8 words) */
    0x0BC, /* BLOCK_KEY1 (8 words) */
    0x0DC, /* BLOCK_KEY2 (8 words) */
    0x0FC, /* BLOCK_KEY3 (8 words) */
    0x11C, /* BLOCK_KEY4 (8 words) */
    0x13C, /* BLOCK_KEY5 (8 words) */
    0x15C, /* BLOCK_SYS_DATA2 (8 words) */
};

/* ESP32-C2 eFuse block read-back offsets (from DR_REG_EFUSE_BASE 0x60008800) */
static const uint32_t efuse_block_offsets_c2[] = {
    0x02C, /* BLOCK0 */
    0x034, /* BLOCK1 */
    0x040, /* BLOCK2 */
    0x060, /* BLOCK_KEY0 */
};

/* ESP32-S2/S3 eFuse block read-back offsets (from EFUSE_BASE)
 * Source: espefuse/efuse/esp32s2/mem_definition.py */
static const uint32_t efuse_block_offsets_s2[] = {
    0x02C, /* BLOCK0 (6 words) */
    0x044, /* BLOCK1/MAC (6 words) */
    0x05C, /* BLOCK2/SYS_DATA (8 words) */
    0x07C, /* BLOCK3/USR_DATA (8 words) */
    0x09C, /* BLOCK_KEY0 (8 words) */
    0x0BC, /* BLOCK_KEY1 (8 words) */
    0x0DC, /* BLOCK_KEY2 (8 words) */
    0x0FC, /* BLOCK_KEY3 (8 words) */
    0x11C, /* BLOCK_KEY4 (8 words) */
    0x13C, /* BLOCK_KEY5 (8 words) */
    0x15C, /* BLOCK_SYS_DATA2 (8 words) */
};

/* ESP32 eFuse block read-back offsets (from EFUSE_RD_REG_BASE 0x3FF5A000)
 * Source: espefuse/efuse/esp32/mem_definition.py */
static const uint32_t efuse_block_offsets_esp32[] = {
    0x000, /* BLOCK0 (7 words) */
    0x038, /* BLOCK1/flash_encryption (8 words) */
    0x058, /* BLOCK2/secure_boot (8 words) */
    0x078, /* BLOCK3/user_data (8 words) */
};

/* ESP32 eFuse block lengths in words */
static const uint8_t efuse_block_lengths_esp32[] = {
    7, /* BLOCK0 */
    8, /* BLOCK1 */
    8, /* BLOCK2 */
    8, /* BLOCK3 */
};

/* ESP32 eFuse block write offsets (from EFUSE_RD_REG_BASE 0x3FF5A000) */
static const uint32_t efuse_block_wr_offsets_esp32[] = {
    0x01C, /* BLOCK0 write */
    0x098, /* BLOCK1 write */
    0x0B8, /* BLOCK2 write */
    0x0D8, /* BLOCK3 write */
};

/*
 * efuse_write32 - Write 32-bit value to eFuse with OR operation
 */
static void efuse_write32(fesp_chip_ctx_t *ctx, int offset, uint32_t val)
{
    if (offset + 3 < ctx->efuse_size) {
#if FESP_HAL_LOG_HAS_DEBUG
        uint8_t b0 = ctx->efuse[offset];
        uint8_t b1 = ctx->efuse[offset + 1];
        uint8_t b2 = ctx->efuse[offset + 2];
        uint8_t b3 = ctx->efuse[offset + 3];
#endif
        ctx->efuse[offset] |= (uint8_t)(val & 0xFF);
        ctx->efuse[offset + 1] |= (uint8_t)((val >> 8) & 0xFF);
        ctx->efuse[offset + 2] |= (uint8_t)((val >> 16) & 0xFF);
        ctx->efuse[offset + 3] |= (uint8_t)((val >> 24) & 0xFF);
        FESP_HAL_LOGD(TAG,
                      "eFuse write: offset=0x%X val=0x%08lX "
                      "before=%02X%02X%02X%02X after=%02X%02X%02X%02X",
                      offset, val, b3, b2, b1, b0, ctx->efuse[offset + 3],
                      ctx->efuse[offset + 2], ctx->efuse[offset + 1],
                      ctx->efuse[offset]);
    }
}

/*
 * fesp_chip_write_reg_esp32 - Handle ESP32 eFuse controller write
 */
bool fesp_chip_write_reg_esp32(fesp_chip_ctx_t *ctx, int offset, uint32_t val)
{
    int num_blocks = (int)(sizeof(efuse_block_wr_offsets_esp32) /
                           sizeof(efuse_block_wr_offsets_esp32[0]));

    /* Check if address is a block write address */
    for (int blk = 0; blk < num_blocks; blk++) {
        int wr_ofs = (int)efuse_block_wr_offsets_esp32[blk];
        if (offset >= wr_ofs && offset < wr_ofs + 32 && (offset & 3) == 0) {
            int word_idx = (offset - wr_ofs) / 4;
            if (word_idx < 8) {
                ctx->pgm_data[blk * 8 + word_idx] = val;
                FESP_HAL_LOGD(TAG, "ESP32 BLOCK%d PGM_DATA%d = 0x%08lX", blk,
                              word_idx, val);
            }
            return true;
        }
    }

    /* CONF_REG */
    if (offset == (int)ctx->efuse_conf_ofs) {
        FESP_HAL_LOGD(TAG, "EFUSE_CONF = 0x%08lX", val);
        return true;
    }

    /* CMD_REG */
    if (offset == (int)ctx->efuse_cmd_ofs) {
        FESP_HAL_LOGD(TAG, "EFUSE_CMD = 0x%08lX", val);
        if (val == 0x1) {
            /* EFUSE_CMD_READ: Copy write registers to read registers */
            for (int blk = 0; blk < num_blocks; blk++) {
                int block_offset = (int)efuse_block_offsets_esp32[blk];
                int block_len = (int)efuse_block_lengths_esp32[blk];
                uint32_t *blk_pgm = &ctx->pgm_data[blk * 8];
                bool has_data = false;
                for (int i = 0; i < block_len; i++) {
                    if (blk_pgm[i] != 0) {
                        has_data = true;
                        break;
                    }
                }
                if (!has_data) {
                    continue;
                }
                for (int i = 0; i < block_len; i++) {
                    efuse_write32(ctx, block_offset + i * 4, blk_pgm[i]);
                }
                FESP_HAL_LOGD(
                    TAG, "ESP32 eFuse BURN block%d at offset 0x%X (%d words)",
                    blk, block_offset, block_len);
            }
            memset(ctx->pgm_data, 0, sizeof(ctx->pgm_data));
        }
        return true;
    }

    return true;
}

/*
 * fesp_chip_write_reg_modern - Handle C2/C3/C6/S2/S3 eFuse controller write
 */
bool fesp_chip_write_reg_modern(fesp_chip_ctx_t *ctx, int offset, uint32_t val)
{
#define EFUSE_PGM_DATA_SIZE 44

    /* PGM_DATA registers */
    if (offset < EFUSE_PGM_DATA_SIZE && (offset & 3) == 0) {
        int idx = offset / 4;
        if (idx < 8) {
            ctx->pgm_data[idx] = val;
            FESP_HAL_LOGD(TAG, "PGM_DATA%d = 0x%08lX", idx, val);
        }
        return true;
    }

    /* CONF_REG */
    if (offset == (int)ctx->efuse_conf_ofs) {
        FESP_HAL_LOGD(TAG, "EFUSE_CONF = 0x%08lX", val);
        return true;
    }

    /* CMD_REG */
    if (offset == (int)ctx->efuse_cmd_ofs) {
        FESP_HAL_LOGD(TAG, "EFUSE_CMD = 0x%08lX", val);
        if (val & 0x02) {
            int block = (int)((val >> 2) & 0xF);
            const uint32_t *block_offsets = NULL;
            int num_blocks = 0;

            switch (ctx->type) {
            case FESP_CHIP_ESP32C3:
            case FESP_CHIP_ESP32C6:
            case FESP_CHIP_ESP32C5:
            case FESP_CHIP_ESP32C61:
            case FESP_CHIP_ESP32H2:
            case FESP_CHIP_ESP32P4:
            case FESP_CHIP_ESP32S31:
                block_offsets = efuse_block_offsets_c3;
                num_blocks = (int)(sizeof(efuse_block_offsets_c3) /
                                   sizeof(efuse_block_offsets_c3[0]));
                break;
            case FESP_CHIP_ESP32S2:
            case FESP_CHIP_ESP32S3:
                block_offsets = efuse_block_offsets_s2;
                num_blocks = (int)(sizeof(efuse_block_offsets_s2) /
                                   sizeof(efuse_block_offsets_s2[0]));
                break;
            case FESP_CHIP_ESP32C2:
                block_offsets = efuse_block_offsets_c2;
                num_blocks = (int)(sizeof(efuse_block_offsets_c2) /
                                   sizeof(efuse_block_offsets_c2[0]));
                break;
            default:
                break;
            }

            if (block_offsets && block < num_blocks) {
                int block_offset = (int)block_offsets[block];
                for (int i = 0; i < 8; i++) {
                    efuse_write32(ctx, block_offset + i * 4, ctx->pgm_data[i]);
                }
                FESP_HAL_LOGD(TAG, "eFuse BURN block%d at offset 0x%X", block,
                              block_offset);
            }
        }
        return true;
    }

#undef EFUSE_PGM_DATA_SIZE

    return true;
}

/*
 * read_efuse_bits - Read bits from eFuse by offset and mask
 *
 * @ctx:    Chip context
 * @offset: uint8_t offset within eFuse array
 * @mask:   Bit mask to apply
 *
 * Returns the masked value shifted to LSB, or 0 if offset is out of range.
 */
static uint32_t read_efuse_bits(const fesp_chip_ctx_t *ctx, int offset,
                                uint32_t mask)
{
    if (!ctx->efuse || offset + 3 >= ctx->efuse_size) {
        return 0;
    }
    uint32_t val = ctx->efuse[offset] |
                   ((uint32_t)ctx->efuse[offset + 1] << 8) |
                   ((uint32_t)ctx->efuse[offset + 2] << 16) |
                   ((uint32_t)ctx->efuse[offset + 3] << 24);
    return val & mask;
}

/*
 * write_efuse_bits - Write (set) bits in eFuse by offset and mask
 *
 * @ctx:    Chip context
 * @offset: uint8_t offset within eFuse array
 * @mask:   Bit mask identifying the field
 * @value:  Value to write (will be shifted to mask position)
 *
 * Performs OR-write: only sets bits, never clears.
 */
static void write_efuse_bits(fesp_chip_ctx_t *ctx, int offset, uint32_t mask,
                             uint32_t value)
{
    if (!ctx->efuse || offset + 3 >= ctx->efuse_size) {
        return;
    }
    int shift = 0;
    uint32_t m = mask;
    while (m && !(m & 1)) {
        shift++;
        m >>= 1;
    }
    uint32_t shifted = (value << shift) & mask;
    for (int i = 0; i < 4; i++) {
        uint32_t byte_mask = (mask >> (i * 8)) & 0xFF;
        if (byte_mask) {
            ctx->efuse[offset + i] |=
                (uint8_t)(shifted >> (i * 8)) & (uint8_t)byte_mask;
        }
    }
}

/*
 * clear_efuse_bits - Clear bits in eFuse by offset and mask
 *
 * Used by the simulator to allow toggling eFuse state for testing.
 * Real eFuse cannot be cleared.
 */
static void clear_efuse_bits(fesp_chip_ctx_t *ctx, int offset, uint32_t mask)
{
    if (!ctx->efuse || offset + 3 >= ctx->efuse_size) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        uint32_t byte_mask = (mask >> (i * 8)) & 0xFF;
        if (byte_mask) {
            ctx->efuse[offset + i] &= ~(uint8_t)byte_mask;
        }
    }
}

/*
 * count_bits - Count number of 1-bits in a value
 */
static int count_bits(uint32_t val)
{
    int count = 0;
    while (val) {
        count += val & 1;
        val >>= 1;
    }
    return count;
}

/*
 * fesp_efuse_apply_block0_defaults - Apply BLOCK0 defaults for missing eFuse
 * fields
 *
 * Fills zero-valued BLOCK0 bytes with chip-specific defaults.
 * Used when loading old .esp files that may not have all BLOCK0 fields.
 * Only writes to bytes that are zero to avoid overwriting user data.
 */
void fesp_efuse_apply_block0_defaults(fesp_chip_ctx_t *ctx)
{
    if (!ctx->efuse) {
        return;
    }

    FESP_HAL_LOGD(TAG,
                  "ApplyBlock0Defaults: word0=%02X%02X%02X%02X, crypt_cfg=%02X",
                  ctx->efuse[0x03], ctx->efuse[0x02], ctx->efuse[0x01],
                  ctx->efuse[0x00], ctx->efuse[0x14]);

    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        /* word0: CHIP_CPU_FREQ_RATED + CONSOLE_DEBUG_DISABLE */
        if (ctx->efuse[0x00] == 0 && ctx->efuse[0x01] == 0 &&
            ctx->efuse[0x02] == 0 && ctx->efuse[0x03] == 0) {
            ctx->efuse[0x01] = 0x10;
            ctx->efuse[0x03] = 0x80;
            FESP_HAL_LOGD(TAG, "ApplyBlock0Defaults: word0 defaults applied");
        }
        /* word5: FLASH_CRYPT_CONFIG at bits[31:28] */
        if (ctx->efuse[0x17] == 0) {
            ctx->efuse[0x17] = 0xF0;
            FESP_HAL_LOGD(TAG,
                          "ApplyBlock0Defaults: FLASH_CRYPT_CONFIG set to 0xF");
        }
        break;
    default:
        break;
    }
}

/*
 * fesp_efuse_get_flash_crypt_cnt - Get flash encryption counter value from
 * eFuse
 *
 * Returns the raw bitfield value. Check if odd number of 1-bits
 * to determine if encryption is enabled.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns raw counter value from eFuse.
 */
uint32_t fesp_efuse_get_flash_crypt_cnt(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_FLASH_CRYPT_CNT_ESP32,
                               FESP_EFUSE_MASK_FLASH_CRYPT_CNT_ESP32) >>
               20;
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S2,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2) >>
               18;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S3,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3) >>
               18;
    case FESP_CHIP_ESP32C2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C2,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2) >>
               7;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C3,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3) >>
               18;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C6,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6) >>
               18;
    case FESP_CHIP_ESP32C5:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C5,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C5) >>
               16;
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S31,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S31) >>
               21;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C61,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C61) >>
               23;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32H2,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32H2) >>
               18;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32P4,
                               FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32P4) >>
               18;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_is_flash_encryption_enabled - Check if flash encryption is active
 *
 * Flash encryption is enabled when FLASH_CRYPT_CNT has odd number of 1-bits.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns true if flash encryption is enabled.
 */
bool fesp_efuse_is_flash_encryption_enabled(const fesp_chip_ctx_t *ctx)
{
    uint32_t cnt = fesp_efuse_get_flash_crypt_cnt(ctx);
    return (count_bits(cnt) & 1) != 0;
}

/*
 * fesp_efuse_is_download_encrypt_disabled - Check if download mode encryption
 * is disabled
 *
 * When disabled, plaintext data can be written to flash in download mode.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns true if download encryption is disabled.
 */
bool fesp_efuse_is_download_encrypt_disabled(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DISABLE_DL_ENCRYPT_ESP32,
                               FESP_EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32) != 0;
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S2,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S3,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3) != 0;
    case FESP_CHIP_ESP32C2:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C2,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C3,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C6,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C5,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C61,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32H2,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32P4,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32P4) != 0;
    default:
        return false;
    }
}

/*
 * fesp_efuse_is_download_decrypt_disabled - Check if download mode decryption
 * is disabled
 *
 * When disabled, encrypted flash data is returned as ciphertext in download
 * mode. Only ESP32 has this field.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns true if download decryption is disabled.
 */
bool fesp_efuse_is_download_decrypt_disabled(const fesp_chip_ctx_t *ctx)
{
    /* Only ESP32 has DISABLE_DL_DECRYPT field */
    if (ctx->type == FESP_CHIP_ESP32) {
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DISABLE_DL_DECRYPT_ESP32,
                               FESP_EFUSE_BIT_DISABLE_DL_DECRYPT_ESP32) != 0;
    }
    return false;
}

/*
 * fesp_efuse_is_download_mode_disabled - Check if download mode is disabled via
 * eFuse
 *
 * When disabled, chip cannot enter download mode via DTR/RTS signals.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns true if download mode is disabled.
 */
bool fesp_efuse_is_download_mode_disabled(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_UART_DOWNLOAD_DIS_ESP32,
                               FESP_EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32) != 0;
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S2,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S3,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3) != 0;
    case FESP_CHIP_ESP32C2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C2,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C3,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C6,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C5,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C61,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32H2,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32P4,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32P4) != 0;
    default:
        return false;
    }
}

/*
 * fesp_efuse_is_secure_download_enabled - Check if secure download mode is
 * enabled
 *
 * In secure download mode, only flash-related commands are allowed.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns true if secure download mode is enabled.
 */
bool fesp_efuse_is_secure_download_enabled(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return false; /* ESP32 does not support secure download mode */
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S2,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S3,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3) != 0;
    case FESP_CHIP_ESP32C2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C2,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C3,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C6,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C5,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C61,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32H2,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32P4,
                               FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32P4) != 0;
    default:
        return false;
    }
}

/*
 * fesp_efuse_get_dl_encrypt_disabled - Get raw eFuse value for download encrypt
 * disabled
 */
uint32_t fesp_efuse_get_dl_encrypt_disabled(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DISABLE_DL_ENCRYPT_ESP32,
                               FESP_EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32) != 0;
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S2,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S3,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3) != 0;
    case FESP_CHIP_ESP32C2:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C2,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C3,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C6,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C5,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C61,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32H2,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(
                   ctx, FESP_EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32P4,
                   FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32P4) != 0;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_get_dl_mode_disabled - Get raw eFuse value for download mode
 * disabled
 */
uint32_t fesp_efuse_get_dl_mode_disabled(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_UART_DOWNLOAD_DIS_ESP32,
                               FESP_EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32) != 0;
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S2,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S3,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3) != 0;
    case FESP_CHIP_ESP32C2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C2,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C3,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C6,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C5,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C61,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32H2,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32P4,
                               FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32P4) != 0;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_get_secure_boot_flag - Get raw eFuse value for secure boot
 *
 * ESP32: returns ABS_DONE_0 | (ABS_DONE_1 << 1).
 * Others: returns SECURE_BOOT_EN bit value.
 */
uint32_t fesp_efuse_get_secure_boot_flag(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32: {
        uint32_t v0 = read_efuse_bits(ctx, FESP_EFUSE_OFFS_ABS_DONE_0_ESP32,
                                      FESP_EFUSE_BIT_ABS_DONE_0_ESP32) != 0;
        uint32_t v1 = read_efuse_bits(ctx, FESP_EFUSE_OFFS_ABS_DONE_1_ESP32,
                                      FESP_EFUSE_BIT_ABS_DONE_1_ESP32) != 0;
        return v0 | (v1 << 1);
    }
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32S2,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32S3,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32S3) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C3,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C6,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C5,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C5) != 0;
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32S31,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32S31) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C61,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32H2,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32P4,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32P4) != 0;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_get_jtag_flag - Get raw eFuse value for JTAG disable
 */
uint32_t fesp_efuse_get_jtag_flag(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_JTAG_DISABLE_ESP32,
                               FESP_EFUSE_BIT_JTAG_DISABLE_ESP32) != 0;
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32S2,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32S3,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32S3) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C3,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C6,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C5,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C61,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32H2,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return 0;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_is_secure_boot_enabled - Check if secure boot is enabled
 *
 * ESP32: ABS_DONE_0 (Secure Boot V1) or ABS_DONE_1 (Secure Boot V2).
 * ESP32-S2/S3/C3/C6: SECURE_BOOT_EN.
 * ESP8266/C2: not supported.
 */
bool fesp_efuse_is_secure_boot_enabled(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32: {
        bool v1 = read_efuse_bits(ctx, FESP_EFUSE_OFFS_ABS_DONE_0_ESP32,
                                  FESP_EFUSE_BIT_ABS_DONE_0_ESP32) != 0;
        bool v2 = read_efuse_bits(ctx, FESP_EFUSE_OFFS_ABS_DONE_1_ESP32,
                                  FESP_EFUSE_BIT_ABS_DONE_1_ESP32) != 0;
        return v1 || v2;
    }
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32S2,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32S3,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32S3) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C3,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C6,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C5,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C5) != 0;
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32S31,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32S31) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32C61,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32H2,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SECURE_BOOT_EN_ESP32P4,
                               FESP_EFUSE_BIT_SECURE_BOOT_EN_ESP32P4) != 0;
    default:
        return false;
    }
}

/*
 * fesp_efuse_is_jtag_disabled - Check if JTAG is disabled via eFuse
 *
 * ESP32: JTAG_DISABLE.
 * ESP32-S2/S3/C3/C6: DIS_PAD_JTAG.
 * ESP8266/C2: not supported.
 */
bool fesp_efuse_is_jtag_disabled(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_JTAG_DISABLE_ESP32,
                               FESP_EFUSE_BIT_JTAG_DISABLE_ESP32) != 0;
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32S2,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32S3,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32S3) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C3,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C6,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C5,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C61,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32H2,
                               FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return 0;
    default:
        return false;
    }
}

/*
 * fesp_efuse_get_jtag_disabled_count - Get number of disabled JTAG interfaces
 */
int fesp_efuse_get_jtag_disabled_count(const fesp_chip_ctx_t *ctx)
{
    int count = 0;
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_JTAG_DISABLE_ESP32,
                                 FESP_EFUSE_BIT_JTAG_DISABLE_ESP32) != 0;
        break;
    case FESP_CHIP_ESP32S2:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32S2,
                                 FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32S2) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S2,
                                 FESP_EFUSE_BIT_SOFT_DIS_JTAG_ESP32S2) != 0;
        break;
    case FESP_CHIP_ESP32S3:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32S3,
                                 FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32S3) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S3,
                                 FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32S3) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32S3,
                                 FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32S3) != 0;
        break;
    case FESP_CHIP_ESP32C3:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C3,
                                 FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C3) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C3,
                                 FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C3) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C3,
                                 FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C3) != 0;
        break;
    case FESP_CHIP_ESP32C6:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C6,
                                 FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C6) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C6,
                                 FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C6) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C6,
                                 FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C6) != 0;
        break;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C5,
                                 FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C5) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C5,
                                 FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C5) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C5,
                                 FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C5) != 0;
        break;
    case FESP_CHIP_ESP32C61:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32C61,
                                 FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32C61) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C61,
                                 FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C61) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C61,
                                 FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C61) != 0;
        break;
    case FESP_CHIP_ESP32H2:
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_PAD_JTAG_ESP32H2,
                                 FESP_EFUSE_BIT_DIS_PAD_JTAG_ESP32H2) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32H2,
                                 FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32H2) != 0;
        count += read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32H2,
                                 FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32H2) != 0;
        break;
    default:
        break;
    }
    return count;
}

/*
 * fesp_efuse_get_jtag_total_count - Get total number of JTAG interfaces
 */
int fesp_efuse_get_jtag_total_count(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        return 1;
    case FESP_CHIP_ESP32S2:
        return 2;
    case FESP_CHIP_ESP32S3:
    case FESP_CHIP_ESP32C3:
    case FESP_CHIP_ESP32C6:
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32C61:
    case FESP_CHIP_ESP32H2:
    case FESP_CHIP_ESP32S31:
        return 3;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_get_soft_jtag_flag - Get raw eFuse value for SOFT_DIS_JTAG
 */
uint32_t fesp_efuse_get_soft_jtag_flag(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32S2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S2,
                               FESP_EFUSE_BIT_SOFT_DIS_JTAG_ESP32S2) != 0;
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S3,
                               FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32S3) >>
               16;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C3,
                               FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C3) >>
               16;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C6,
                               FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C6) >>
               16;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C5,
                               FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C5) >>
               16;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C61,
                               FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32C61) >>
               16;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_SOFT_DIS_JTAG_ESP32H2,
                               FESP_EFUSE_MASK_SOFT_DIS_JTAG_ESP32H2) >>
               16;
    case FESP_CHIP_ESP32P4:
        return 0;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_get_usb_jtag_flag - Get raw eFuse value for DIS_USB_JTAG
 */
uint32_t fesp_efuse_get_usb_jtag_flag(const fesp_chip_ctx_t *ctx)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32S3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32S3,
                               FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32S3) != 0;
    case FESP_CHIP_ESP32C3:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C3,
                               FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C3) != 0;
    case FESP_CHIP_ESP32C6:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C6,
                               FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C6) != 0;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C5,
                               FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C5) != 0;
    case FESP_CHIP_ESP32C61:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32C61,
                               FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32C61) != 0;
    case FESP_CHIP_ESP32H2:
        return read_efuse_bits(ctx, FESP_EFUSE_OFFS_DIS_USB_JTAG_ESP32H2,
                               FESP_EFUSE_BIT_DIS_USB_JTAG_ESP32H2) != 0;
    case FESP_CHIP_ESP32P4:
        return 0;
    default:
        return 0;
    }
}

/*
 * fesp_efuse_get_key_purpose - Get key block purpose from eFuse
 *
 * For S2/S3/C3/C6: reads FESP_KEY_PURPOSE_N field from eFuse.
 * For ESP32: hardcoded (BLOCK1=encryption, BLOCK2=secure boot).
 * For C2: hardcoded (BLOCK_KEY0=encryption, only one key block).
 *
 * @ctx:   Pointer to chip context (const, read-only)
 * @block: Key block index (0 = KEY0/FESP_KEY_PURPOSE_0, 1 = KEY1, etc.)
 *
 * Returns key purpose value (FESP_KEY_PURPOSE_*).
 */
uint8_t fesp_efuse_get_key_purpose(const fesp_chip_ctx_t *ctx, int block)
{
    if (!ctx->efuse || block < 0) {
        return FESP_KEY_PURPOSE_USER;
    }

    /* ESP32: fixed key block assignments (no KEY_PURPOSE fields) */
    if (ctx->type == FESP_CHIP_ESP32) {
        if (block == 0) {
            return FESP_KEY_PURPOSE_XTS_AES_128_KEY; /* BLOCK1 = flash
                                                        encryption */
        }
        return FESP_KEY_PURPOSE_USER;
    }

    /* ESP32-C2: only one key block, always flash encryption */
    if (ctx->type == FESP_CHIP_ESP32C2) {
        if (block == 0) {
            return FESP_KEY_PURPOSE_XTS_AES_128_KEY;
        }
        return FESP_KEY_PURPOSE_USER;
    }

    /* ESP32-S31: 5 keys (KEY0-KEY4) at EFUSE_BASE+0x38, 5-bit fields */
    if (ctx->type == FESP_CHIP_ESP32S31) {
        if (block > 4) {
            return FESP_KEY_PURPOSE_USER;
        }
        static const uint8_t s31_shifts[] = {0, 5, 10, 15, 20};
        uint32_t mask = 0x1FUL << s31_shifts[block];
        return (uint8_t)(read_efuse_bits(ctx, 0x38, mask) >> s31_shifts[block]);
    }

    /* S2/S3/C3/C6/C5/C61/H2/P4: read KEY_PURPOSE from eFuse.
       BLOCK0 base varies by chip: ESP32=0x00, S2/S3/C3/C6=0x2C.
       KEY_PURPOSE offsets are relative to BLOCK0 base. */
    if (block > 5) {
        return FESP_KEY_PURPOSE_USER;
    }

    static const uint32_t purpose_masks[] = {
        FESP_EFUSE_MASK_FESP_KEY_PURPOSE_0, FESP_EFUSE_MASK_FESP_KEY_PURPOSE_1,
        FESP_EFUSE_MASK_FESP_KEY_PURPOSE_2, FESP_EFUSE_MASK_FESP_KEY_PURPOSE_3,
        FESP_EFUSE_MASK_FESP_KEY_PURPOSE_4, FESP_EFUSE_MASK_FESP_KEY_PURPOSE_5,
    };
    static const uint8_t purpose_shifts[] = {24, 28, 0, 4, 8, 12};

    /* Chip-specific BLOCK0 base offset in eFuse array */
    int block0_base =
        (ctx->type == FESP_CHIP_ESP32S2 || ctx->type == FESP_CHIP_ESP32S3 ||
         ctx->type == FESP_CHIP_ESP32C3 || ctx->type == FESP_CHIP_ESP32C6 ||
         ctx->type == FESP_CHIP_ESP32C5 || ctx->type == FESP_CHIP_ESP32C61 ||
         ctx->type == FESP_CHIP_ESP32H2 || ctx->type == FESP_CHIP_ESP32P4)
            ? 0x2C
            : 0x00;

    /* FESP_KEY_PURPOSE_N offsets relative to BLOCK0: word2=0x08, word3=0x0C */
    static const uint8_t rel_offsets[] = {0x08, 0x08, 0x0C, 0x0C, 0x0C, 0x0C};

    int offset = block0_base + rel_offsets[block];
    uint32_t mask = purpose_masks[block];
    int shift = purpose_shifts[block];

    return (uint8_t)(read_efuse_bits(ctx, offset, mask) >> shift);
}

/*
 * fesp_efuse_set_key_purpose - Set key block purpose in eFuse
 *
 * For S2/S3/C3/C6: writes FESP_KEY_PURPOSE_N field to eFuse.
 * For ESP32/C2: no-op (fixed key assignments).
 *
 * Simulator only: directly modifies eFuse array.
 */
void fesp_efuse_set_key_purpose(fesp_chip_ctx_t *ctx, int block,
                                uint8_t purpose)
{
    if (!ctx->efuse || block < 0 || block > 5) {
        return;
    }

    /* ESP32/C2: fixed purpose, cannot change */
    if (ctx->type == FESP_CHIP_ESP32 || ctx->type == FESP_CHIP_ESP32C2) {
        return;
    }

    /* ESP32-S31: 5 keys (KEY0-KEY4) at EFUSE_BASE+0x38, 5-bit fields */
    if (ctx->type == FESP_CHIP_ESP32S31) {
        if (block > 4) {
            return;
        }
        static const uint8_t s31_shifts[] = {0, 5, 10, 15, 20};
        uint32_t mask = 0x1FUL << s31_shifts[block];
        clear_efuse_bits(ctx, 0x38, mask);
        write_efuse_bits(ctx, 0x38, mask, purpose);
        return;
    }

    /* ESP32-S3/C3/C6/C5/C61/H2/P4: KEY5 cannot have XTS_AES purposes (hardware
     * bug) */
    if ((ctx->type == FESP_CHIP_ESP32S3 || ctx->type == FESP_CHIP_ESP32C3 ||
         ctx->type == FESP_CHIP_ESP32C6 || ctx->type == FESP_CHIP_ESP32C5 ||
         ctx->type == FESP_CHIP_ESP32C61 || ctx->type == FESP_CHIP_ESP32H2 ||
         ctx->type == FESP_CHIP_ESP32P4) &&
        block == 5) {
        if (purpose == FESP_KEY_PURPOSE_XTS_AES_128_KEY ||
            purpose == FESP_KEY_PURPOSE_XTS_AES_256_KEY_1 ||
            purpose == FESP_KEY_PURPOSE_XTS_AES_256_KEY_2) {
            return;
        }
    }

    static const uint32_t purpose_masks[] = {
        FESP_EFUSE_MASK_FESP_KEY_PURPOSE_0, FESP_EFUSE_MASK_FESP_KEY_PURPOSE_1,
        FESP_EFUSE_MASK_FESP_KEY_PURPOSE_2, FESP_EFUSE_MASK_FESP_KEY_PURPOSE_3,
        FESP_EFUSE_MASK_FESP_KEY_PURPOSE_4, FESP_EFUSE_MASK_FESP_KEY_PURPOSE_5,
    };

    /* Chip-specific BLOCK0 base offset in eFuse array */
    int block0_base =
        (ctx->type == FESP_CHIP_ESP32S2 || ctx->type == FESP_CHIP_ESP32S3 ||
         ctx->type == FESP_CHIP_ESP32C3 || ctx->type == FESP_CHIP_ESP32C6 ||
         ctx->type == FESP_CHIP_ESP32C5 || ctx->type == FESP_CHIP_ESP32C61 ||
         ctx->type == FESP_CHIP_ESP32H2 || ctx->type == FESP_CHIP_ESP32P4)
            ? 0x2C
            : 0x00;
    static const uint8_t rel_offsets[] = {0x08, 0x08, 0x0C, 0x0C, 0x0C, 0x0C};

    int offset = block0_base + rel_offsets[block];
    uint32_t mask = purpose_masks[block];

    clear_efuse_bits(ctx, offset, mask);
    write_efuse_bits(ctx, offset, mask, purpose);
}

/*
 * fesp_efuse_get_encryption_key_offset - Get eFuse offset and length of flash
 * encryption key
 *
 * For ESP32: BLOCK1 at offset 0x38 (fixed).
 * For C2: BLOCK_KEY0 at offset 0x60 (fixed).
 * For S2/S3/C3/C6: scans KEY_PURPOSE fields to find XTS_AES key block.
 */
int fesp_efuse_get_encryption_key_offset(const fesp_chip_ctx_t *ctx,
                                         int *key_len)
{
    if (!key_len) {
        return -1;
    }

    /* ESP32: BLOCK1 at fixed offset (no KEY_PURPOSE fields) */
    if (ctx->type == FESP_CHIP_ESP32) {
        *key_len = 32;
        return 0x38;
    }

    /* ESP32-C2: BLOCK_KEY0 at fixed offset (only one key block) */
    if (ctx->type == FESP_CHIP_ESP32C2) {
        *key_len = 32;
        return 0x60;
    }

    /* ESP32-S31: 5 key blocks, scan KEY_PURPOSE at 0x38 */
    if (ctx->type == FESP_CHIP_ESP32S31) {
        static const uint32_t s31_key_block_offsets[] = {0x9C, 0xBC, 0xDC, 0xFC,
                                                         0x11C};
        for (int i = 0; i < 5; i++) {
            uint8_t purpose = fesp_efuse_get_key_purpose(ctx, i);
            if (purpose == FESP_KEY_PURPOSE_XTS_AES_128_KEY ||
                purpose == FESP_KEY_PURPOSE_XTS_AES_256_KEY_1 ||
                purpose == FESP_KEY_PURPOSE_XTS_AES_256_KEY_2) {
                *key_len = 32;
                return (int)s31_key_block_offsets[i];
            }
        }
        *key_len = 0;
        return -1;
    }

    /* S2/S3/C3/C6: scan KEY_PURPOSE fields to find XTS_AES key block */
    static const uint32_t key_block_offsets[] = {0x9C, 0xBC,  0xDC,
                                                 0xFC, 0x11C, 0x13C};

    for (int i = 0; i < 6; i++) {
        uint8_t purpose = fesp_efuse_get_key_purpose(ctx, i);
        if (purpose == FESP_KEY_PURPOSE_XTS_AES_128_KEY ||
            purpose == FESP_KEY_PURPOSE_XTS_AES_256_KEY_1 ||
            purpose == FESP_KEY_PURPOSE_XTS_AES_256_KEY_2) {
            *key_len = 32;
            return (int)key_block_offsets[i];
        }
    }

    /* No encryption key found */
    *key_len = 0;
    return -1;
}

/*
 * fesp_efuse_set_flash_encryption - Set flash encryption state via eFuse
 *
 * @mode: 0 = no encryption, 1 = dev (encrypted), 2 = release (encrypted + no
 * manual encrypt)
 *
 * Simulator only: directly modifies eFuse array (clears then sets bits).
 */
void fesp_efuse_set_flash_encryption(fesp_chip_ctx_t *ctx, int mode)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        clear_efuse_bits(ctx, 0x00, FESP_EFUSE_MASK_FLASH_CRYPT_CNT_ESP32);
        clear_efuse_bits(ctx, 0x18, FESP_EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x00, FESP_EFUSE_MASK_FLASH_CRYPT_CNT_ESP32,
                             1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x18, FESP_EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32,
                             1);
        }
        break;
    case FESP_CHIP_ESP32S2:
        clear_efuse_bits(ctx, 0x34, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2, 1);
        }
        break;
    case FESP_CHIP_ESP32S3:
        clear_efuse_bits(ctx, 0x34, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3, 1);
        }
        break;
    case FESP_CHIP_ESP32C2:
        clear_efuse_bits(ctx, 0x30, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2, 1);
        }
        break;
    case FESP_CHIP_ESP32C3:
        clear_efuse_bits(ctx, 0x34, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3, 1);
        }
        break;
    case FESP_CHIP_ESP32C6:
        clear_efuse_bits(ctx, 0x34, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6, 1);
        }
        break;
    case FESP_CHIP_ESP32C5:
        clear_efuse_bits(ctx, 0x34, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C5);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C5);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C5, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C5, 1);
        }
        break;
    case FESP_CHIP_ESP32S31:
        clear_efuse_bits(ctx, 0x34,
                         FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S31);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S31);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S31, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S31, 1);
        }
        break;
    case FESP_CHIP_ESP32C61:
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C61);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C61);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C61, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C61, 1);
        }
        break;
    case FESP_CHIP_ESP32H2:
        clear_efuse_bits(ctx, 0x34, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32H2);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32H2);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32H2, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32H2, 1);
        }
        break;
    case FESP_CHIP_ESP32P4:
        clear_efuse_bits(ctx, 0x34, FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32P4);
        clear_efuse_bits(ctx, 0x30,
                         FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32P4);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x34,
                             FESP_EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32P4, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32P4, 1);
        }
        break;
    default:
        break;
    }
}

/*
 * fesp_efuse_set_download_mode - Set download mode state via eFuse
 *
 * @mode: 0 = normal, 1 = secure, 2 = disabled
 *
 * Simulator only: directly modifies eFuse array (clears then sets bits).
 */
void fesp_efuse_set_download_mode(fesp_chip_ctx_t *ctx, int mode)
{
    switch (ctx->type) {
    case FESP_CHIP_ESP32:
        clear_efuse_bits(ctx, 0x00, FESP_EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32);
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x00, FESP_EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32,
                             1);
        }
        break;
    case FESP_CHIP_ESP32S2:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2, 1);
        }
        break;
    case FESP_CHIP_ESP32S3:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3, 1);
        }
        break;
    case FESP_CHIP_ESP32C2:
        clear_efuse_bits(ctx, 0x30, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2);
        clear_efuse_bits(ctx, 0x30, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x30,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2, 1);
        }
        break;
    case FESP_CHIP_ESP32C3:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3, 1);
        }
        break;
    case FESP_CHIP_ESP32C6:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6, 1);
        }
        break;
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32S31:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C5);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C5);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C5, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C5, 1);
        }
        break;
    case FESP_CHIP_ESP32C61:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C61);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C61);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C61, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C61, 1);
        }
        break;
    case FESP_CHIP_ESP32H2:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32H2);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32H2);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32H2, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32H2, 1);
        }
        break;
    case FESP_CHIP_ESP32P4:
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32P4);
        clear_efuse_bits(ctx, 0x3C, FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32P4);
        if (mode >= 1) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_ENABLE_SECURITY_DL_ESP32P4, 1);
        }
        if (mode >= 2) {
            write_efuse_bits(ctx, 0x3C,
                             FESP_EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32P4, 1);
        }
        break;
    default:
        break;
    }
}
