/*
 * efuse.c - eFuse controller simulation and field queries for ESP chips.
 */

#include "efuse.h"
#include "chip.h"
#include "../utils/trace.h"
#include <stdio.h>
#include <string.h>

#if ENABLE_TRACE
static const char *TAG = "EFUSE";
#endif

/* ESP32-C3 eFuse block read-back offsets (from DR_REG_EFUSE_BASE 0x60008800)
 * Source: espefuse/efuse/esp32c3/mem_definition.py __base_rd_regs offsets */
static const DWORD efuse_block_offsets_c3[] = {
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
static const DWORD efuse_block_offsets_c2[] = {
    0x02C, /* BLOCK0 */
    0x034, /* BLOCK1 */
    0x040, /* BLOCK2 */
    0x060, /* BLOCK_KEY0 */
};

/* ESP32-S2/S3 eFuse block read-back offsets (from EFUSE_BASE)
 * Source: espefuse/efuse/esp32s2/mem_definition.py */
static const DWORD efuse_block_offsets_s2[] = {
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
static const DWORD efuse_block_offsets_esp32[] = {
    0x000, /* BLOCK0 (7 words) */
    0x038, /* BLOCK1/flash_encryption (8 words) */
    0x058, /* BLOCK2/secure_boot (8 words) */
    0x078, /* BLOCK3/user_data (8 words) */
};

/* ESP32 eFuse block lengths in words */
static const BYTE efuse_block_lengths_esp32[] = {
    7, /* BLOCK0 */
    8, /* BLOCK1 */
    8, /* BLOCK2 */
    8, /* BLOCK3 */
};

/* ESP32 eFuse block write offsets (from EFUSE_RD_REG_BASE 0x3FF5A000) */
static const DWORD efuse_block_wr_offsets_esp32[] = {
    0x01C, /* BLOCK0 write */
    0x098, /* BLOCK1 write */
    0x0B8, /* BLOCK2 write */
    0x0D8, /* BLOCK3 write */
};

/*
 * EfuseWrite32 - Write 32-bit value to eFuse with OR operation
 */
static void EfuseWrite32(CHIP_CTX *ctx, int offset, DWORD val)
{
    if (offset + 3 < ctx->efuse_size) {
#ifdef ENABLE_TRACE_PROTO
        BYTE b0 = ctx->efuse[offset];
        BYTE b1 = ctx->efuse[offset + 1];
        BYTE b2 = ctx->efuse[offset + 2];
        BYTE b3 = ctx->efuse[offset + 3];
#endif
        ctx->efuse[offset] |= (BYTE)(val & 0xFF);
        ctx->efuse[offset + 1] |= (BYTE)((val >> 8) & 0xFF);
        ctx->efuse[offset + 2] |= (BYTE)((val >> 16) & 0xFF);
        ctx->efuse[offset + 3] |= (BYTE)((val >> 24) & 0xFF);
        TRACE_PROTO(TAG,
                    "eFuse write: offset=0x%X val=0x%08lX "
                    "before=%02X%02X%02X%02X after=%02X%02X%02X%02X",
                    offset, val, b3, b2, b1, b0, ctx->efuse[offset + 3],
                    ctx->efuse[offset + 2], ctx->efuse[offset + 1],
                    ctx->efuse[offset]);
    }
}

/*
 * Chip_WriteRegEsp32 - Handle ESP32 eFuse controller write
 */
BOOL Chip_WriteRegEsp32(CHIP_CTX *ctx, int offset, DWORD val)
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
                TRACE_PROTO(TAG, "ESP32 BLOCK%d PGM_DATA%d = 0x%08lX", blk,
                            word_idx, val);
            }
            return TRUE;
        }
    }

    /* CONF_REG */
    if (offset == (int)ctx->efuse_conf_ofs) {
        TRACE_PROTO(TAG, "EFUSE_CONF = 0x%08lX", val);
        return TRUE;
    }

    /* CMD_REG */
    if (offset == (int)ctx->efuse_cmd_ofs) {
        TRACE_PROTO(TAG, "EFUSE_CMD = 0x%08lX", val);
        if (val == 0x1) {
            /* EFUSE_CMD_READ: Copy write registers to read registers */
            for (int blk = 0; blk < num_blocks; blk++) {
                int block_offset = (int)efuse_block_offsets_esp32[blk];
                int block_len = (int)efuse_block_lengths_esp32[blk];
                DWORD *blk_pgm = &ctx->pgm_data[blk * 8];
                BOOL has_data = FALSE;
                for (int i = 0; i < block_len; i++) {
                    if (blk_pgm[i] != 0) {
                        has_data = TRUE;
                        break;
                    }
                }
                if (!has_data) {
                    continue;
                }
                for (int i = 0; i < block_len; i++) {
                    EfuseWrite32(ctx, block_offset + i * 4, blk_pgm[i]);
                }
                TRACE_PROTO(
                    TAG, "ESP32 eFuse BURN block%d at offset 0x%X (%d words)",
                    blk, block_offset, block_len);
            }
            memset(ctx->pgm_data, 0, sizeof(ctx->pgm_data));
        }
        return TRUE;
    }

    return TRUE;
}

/*
 * Chip_WriteRegModern - Handle C2/C3/C6/S2/S3 eFuse controller write
 */
BOOL Chip_WriteRegModern(CHIP_CTX *ctx, int offset, DWORD val)
{
#define EFUSE_PGM_DATA_SIZE 44

    /* PGM_DATA registers */
    if (offset < EFUSE_PGM_DATA_SIZE && (offset & 3) == 0) {
        int idx = offset / 4;
        if (idx < 8) {
            ctx->pgm_data[idx] = val;
            TRACE_PROTO(TAG, "PGM_DATA%d = 0x%08lX", idx, val);
        }
        return TRUE;
    }

    /* CONF_REG */
    if (offset == (int)ctx->efuse_conf_ofs) {
        TRACE_PROTO(TAG, "EFUSE_CONF = 0x%08lX", val);
        return TRUE;
    }

    /* CMD_REG */
    if (offset == (int)ctx->efuse_cmd_ofs) {
        TRACE_PROTO(TAG, "EFUSE_CMD = 0x%08lX", val);
        if (val & 0x02) {
            int block = (int)((val >> 2) & 0xF);
            const DWORD *block_offsets = NULL;
            int num_blocks = 0;

            switch (ctx->type) {
            case CHIP_ESP32C3:
            case CHIP_ESP32C6:
                block_offsets = efuse_block_offsets_c3;
                num_blocks = (int)(sizeof(efuse_block_offsets_c3) /
                                   sizeof(efuse_block_offsets_c3[0]));
                break;
            case CHIP_ESP32S2:
            case CHIP_ESP32S3:
                block_offsets = efuse_block_offsets_s2;
                num_blocks = (int)(sizeof(efuse_block_offsets_s2) /
                                   sizeof(efuse_block_offsets_s2[0]));
                break;
            case CHIP_ESP32C2:
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
                    EfuseWrite32(ctx, block_offset + i * 4, ctx->pgm_data[i]);
                }
                TRACE_PROTO(TAG, "eFuse BURN block%d at offset 0x%X", block,
                            block_offset);
            }
        }
        return TRUE;
    }

#undef EFUSE_PGM_DATA_SIZE

    return TRUE;
}

/*
 * ReadEfuseBits - Read bits from eFuse by offset and mask
 *
 * @ctx:    Chip context
 * @offset: Byte offset within eFuse array
 * @mask:   Bit mask to apply
 *
 * Returns the masked value shifted to LSB, or 0 if offset is out of range.
 */
static DWORD ReadEfuseBits(const CHIP_CTX *ctx, int offset, DWORD mask)
{
    if (!ctx->efuse || offset + 3 >= ctx->efuse_size) {
        return 0;
    }
    DWORD val = ctx->efuse[offset] | ((DWORD)ctx->efuse[offset + 1] << 8) |
                ((DWORD)ctx->efuse[offset + 2] << 16) |
                ((DWORD)ctx->efuse[offset + 3] << 24);
    return val & mask;
}

/*
 * WriteEfuseBits - Write (set) bits in eFuse by offset and mask
 *
 * @ctx:    Chip context
 * @offset: Byte offset within eFuse array
 * @mask:   Bit mask identifying the field
 * @value:  Value to write (will be shifted to mask position)
 *
 * Performs OR-write: only sets bits, never clears.
 */
static void WriteEfuseBits(CHIP_CTX *ctx, int offset, DWORD mask, DWORD value)
{
    if (!ctx->efuse || offset + 3 >= ctx->efuse_size) {
        return;
    }
    int shift = 0;
    DWORD m = mask;
    while (m && !(m & 1)) {
        shift++;
        m >>= 1;
    }
    DWORD shifted = (value << shift) & mask;
    for (int i = 0; i < 4; i++) {
        DWORD byte_mask = (mask >> (i * 8)) & 0xFF;
        if (byte_mask)
            ctx->efuse[offset + i] |=
                (BYTE)(shifted >> (i * 8)) & (BYTE)byte_mask;
    }
}

/*
 * ClearEfuseBits - Clear bits in eFuse by offset and mask
 *
 * Used by the simulator to allow toggling eFuse state for testing.
 * Real eFuse cannot be cleared.
 */
static void ClearEfuseBits(CHIP_CTX *ctx, int offset, DWORD mask)
{
    if (!ctx->efuse || offset + 3 >= ctx->efuse_size) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        DWORD byte_mask = (mask >> (i * 8)) & 0xFF;
        if (byte_mask) {
            ctx->efuse[offset + i] &= ~(BYTE)byte_mask;
        }
    }
}

/*
 * CountBits - Count number of 1-bits in a value
 */
static int CountBits(DWORD val)
{
    int count = 0;
    while (val) {
        count += val & 1;
        val >>= 1;
    }
    return count;
}

/*
 * Efuse_ApplyBlock0Defaults - Apply BLOCK0 defaults for missing eFuse fields
 *
 * Fills zero-valued BLOCK0 bytes with chip-specific defaults.
 * Used when loading old .esp files that may not have all BLOCK0 fields.
 * Only writes to bytes that are zero to avoid overwriting user data.
 */
void Efuse_ApplyBlock0Defaults(CHIP_CTX *ctx)
{
    if (!ctx->efuse) {
        return;
    }

    TRACE_PROTO(TAG,
                "ApplyBlock0Defaults: word0=%02X%02X%02X%02X, crypt_cfg=%02X",
                ctx->efuse[0x03], ctx->efuse[0x02], ctx->efuse[0x01],
                ctx->efuse[0x00], ctx->efuse[0x14]);

    switch (ctx->type) {
    case CHIP_ESP32:
        /* word0: CHIP_CPU_FREQ_RATED + CONSOLE_DEBUG_DISABLE */
        if (ctx->efuse[0x00] == 0 && ctx->efuse[0x01] == 0 &&
            ctx->efuse[0x02] == 0 && ctx->efuse[0x03] == 0) {
            ctx->efuse[0x01] = 0x10;
            ctx->efuse[0x03] = 0x80;
            TRACE_PROTO(TAG, "ApplyBlock0Defaults: word0 defaults applied");
        }
        /* word5: FLASH_CRYPT_CONFIG at bits[31:28] */
        if (ctx->efuse[0x17] == 0) {
            ctx->efuse[0x17] = 0xF0;
            TRACE_FW(TAG, "ApplyBlock0Defaults: FLASH_CRYPT_CONFIG set to 0xF");
        }
        break;
    default:
        break;
    }
}

/*
 * Efuse_GetFlashCryptCnt - Get flash encryption counter value from eFuse
 *
 * Returns the raw bitfield value. Check if odd number of 1-bits
 * to determine if encryption is enabled.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns raw counter value from eFuse.
 */
DWORD Efuse_GetFlashCryptCnt(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return ReadEfuseBits(ctx, EFUSE_OFFS_FLASH_CRYPT_CNT_ESP32,
                             EFUSE_MASK_FLASH_CRYPT_CNT_ESP32) >>
               20;
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S2,
                             EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2) >>
               18;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32S3,
                             EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3) >>
               18;
    case CHIP_ESP32C2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C2,
                             EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2) >>
               7;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C3,
                             EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3) >>
               18;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SPI_BOOT_CRYPT_CNT_ESP32C6,
                             EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6) >>
               18;
    default:
        return 0;
    }
}

/*
 * Efuse_IsFlashEncryptionEnabled - Check if flash encryption is active
 *
 * Flash encryption is enabled when FLASH_CRYPT_CNT has odd number of 1-bits.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns TRUE if flash encryption is enabled.
 */
BOOL Efuse_IsFlashEncryptionEnabled(const CHIP_CTX *ctx)
{
    DWORD cnt = Efuse_GetFlashCryptCnt(ctx);
    return (CountBits(cnt) & 1) != 0;
}

/*
 * Efuse_IsDownloadEncryptDisabled - Check if download mode encryption is
 * disabled
 *
 * When disabled, plaintext data can be written to flash in download mode.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns TRUE if download encryption is disabled.
 */
BOOL Efuse_IsDownloadEncryptDisabled(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DISABLE_DL_ENCRYPT_ESP32,
                             EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32) != 0;
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S2,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S3,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3) != 0;
    case CHIP_ESP32C2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C2,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C3,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C6,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6) != 0;
    default:
        return FALSE;
    }
}

/*
 * Efuse_IsDownloadDecryptDisabled - Check if download mode decryption is
 * disabled
 *
 * When disabled, encrypted flash data is returned as ciphertext in download
 * mode. Only ESP32 has this field.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns TRUE if download decryption is disabled.
 */
BOOL Efuse_IsDownloadDecryptDisabled(const CHIP_CTX *ctx)
{
    /* Only ESP32 has DISABLE_DL_DECRYPT field */
    if (ctx->type == CHIP_ESP32)
        return ReadEfuseBits(ctx, EFUSE_OFFS_DISABLE_DL_DECRYPT_ESP32,
                             EFUSE_BIT_DISABLE_DL_DECRYPT_ESP32) != 0;
    return FALSE;
}

/*
 * Efuse_IsDownloadModeDisabled - Check if download mode is disabled via eFuse
 *
 * When disabled, chip cannot enter download mode via DTR/RTS signals.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns TRUE if download mode is disabled.
 */
BOOL Efuse_IsDownloadModeDisabled(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return ReadEfuseBits(ctx, EFUSE_OFFS_UART_DOWNLOAD_DIS_ESP32,
                             EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32) != 0;
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S2,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S3,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3) != 0;
    case CHIP_ESP32C2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C2,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C3,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C6,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6) != 0;
    default:
        return FALSE;
    }
}

/*
 * Efuse_IsSecureDownloadEnabled - Check if secure download mode is enabled
 *
 * In secure download mode, only flash-related commands are allowed.
 *
 * @ctx: Pointer to chip context (const, read-only)
 *
 * Returns TRUE if secure download mode is enabled.
 */
BOOL Efuse_IsSecureDownloadEnabled(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return FALSE; /* ESP32 does not support secure download mode */
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S2,
                             EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32S3,
                             EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3) != 0;
    case CHIP_ESP32C2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C2,
                             EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C3,
                             EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_ENABLE_SECURITY_DL_ESP32C6,
                             EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6) != 0;
    default:
        return FALSE;
    }
}

/*
 * Efuse_GetDlEncryptDisabled - Get raw eFuse value for download encrypt disabled
 */
DWORD Efuse_GetDlEncryptDisabled(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DISABLE_DL_ENCRYPT_ESP32,
                             EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32) != 0;
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S2,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32S3,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3) != 0;
    case CHIP_ESP32C2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C2,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C3,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DL_MANUAL_ENCRYPT_ESP32C6,
                             EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6) != 0;
    default:
        return 0;
    }
}

/*
 * Efuse_GetDlModeDisabled - Get raw eFuse value for download mode disabled
 */
DWORD Efuse_GetDlModeDisabled(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return ReadEfuseBits(ctx, EFUSE_OFFS_UART_DOWNLOAD_DIS_ESP32,
                             EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32) != 0;
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S2,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32S3,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3) != 0;
    case CHIP_ESP32C2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C2,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C3,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_DOWNLOAD_MODE_ESP32C6,
                             EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6) != 0;
    default:
        return 0;
    }
}

/*
 * Efuse_GetSecureBootFlag - Get raw eFuse value for secure boot
 *
 * ESP32: returns ABS_DONE_0 | (ABS_DONE_1 << 1).
 * Others: returns SECURE_BOOT_EN bit value.
 */
DWORD Efuse_GetSecureBootFlag(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32: {
        DWORD v0 = ReadEfuseBits(ctx, EFUSE_OFFS_ABS_DONE_0_ESP32,
                                 EFUSE_BIT_ABS_DONE_0_ESP32) != 0;
        DWORD v1 = ReadEfuseBits(ctx, EFUSE_OFFS_ABS_DONE_1_ESP32,
                                 EFUSE_BIT_ABS_DONE_1_ESP32) != 0;
        return v0 | (v1 << 1);
    }
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32S2,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32S3,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32S3) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32C3,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32C6,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32C6) != 0;
    default:
        return 0;
    }
}

/*
 * Efuse_GetJtagFlag - Get raw eFuse value for JTAG disable
 */
DWORD Efuse_GetJtagFlag(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return ReadEfuseBits(ctx, EFUSE_OFFS_JTAG_DISABLE_ESP32,
                             EFUSE_BIT_JTAG_DISABLE_ESP32) != 0;
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32S2,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32S3,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32S3) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32C3,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32C6,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32C6) != 0;
    default:
        return 0;
    }
}

/*
 * Efuse_IsSecureBootEnabled - Check if secure boot is enabled
 *
 * ESP32: ABS_DONE_0 (Secure Boot V1) or ABS_DONE_1 (Secure Boot V2).
 * ESP32-S2/S3/C3/C6: SECURE_BOOT_EN.
 * ESP8266/C2: not supported.
 */
BOOL Efuse_IsSecureBootEnabled(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32: {
        BOOL v1 = ReadEfuseBits(ctx, EFUSE_OFFS_ABS_DONE_0_ESP32,
                                EFUSE_BIT_ABS_DONE_0_ESP32) != 0;
        BOOL v2 = ReadEfuseBits(ctx, EFUSE_OFFS_ABS_DONE_1_ESP32,
                                EFUSE_BIT_ABS_DONE_1_ESP32) != 0;
        return v1 || v2;
    }
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32S2,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32S3,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32S3) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32C3,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SECURE_BOOT_EN_ESP32C6,
                             EFUSE_BIT_SECURE_BOOT_EN_ESP32C6) != 0;
    default:
        return FALSE;
    }
}

/*
 * Efuse_IsJtagDisabled - Check if JTAG is disabled via eFuse
 *
 * ESP32: JTAG_DISABLE.
 * ESP32-S2/S3/C3/C6: DIS_PAD_JTAG.
 * ESP8266/C2: not supported.
 */
BOOL Efuse_IsJtagDisabled(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return ReadEfuseBits(ctx, EFUSE_OFFS_JTAG_DISABLE_ESP32,
                             EFUSE_BIT_JTAG_DISABLE_ESP32) != 0;
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32S2,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32S3,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32S3) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32C3,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32C6,
                             EFUSE_BIT_DIS_PAD_JTAG_ESP32C6) != 0;
    default:
        return FALSE;
    }
}

/*
 * Efuse_GetJtagDisabledCount - Get number of disabled JTAG interfaces
 */
int Efuse_GetJtagDisabledCount(const CHIP_CTX *ctx)
{
    int count = 0;
    switch (ctx->type) {
    case CHIP_ESP32:
        count += ReadEfuseBits(ctx, EFUSE_OFFS_JTAG_DISABLE_ESP32,
                               EFUSE_BIT_JTAG_DISABLE_ESP32) != 0;
        break;
    case CHIP_ESP32S2:
        count += ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32S2,
                               EFUSE_BIT_DIS_PAD_JTAG_ESP32S2) != 0;
        count += ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S2,
                               EFUSE_BIT_SOFT_DIS_JTAG_ESP32S2) != 0;
        break;
    case CHIP_ESP32S3:
        count += ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32S3,
                               EFUSE_BIT_DIS_PAD_JTAG_ESP32S3) != 0;
        count += ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S3,
                               EFUSE_MASK_SOFT_DIS_JTAG_ESP32S3) != 0;
        count += ReadEfuseBits(ctx, EFUSE_OFFS_DIS_USB_JTAG_ESP32S3,
                               EFUSE_BIT_DIS_USB_JTAG_ESP32S3) != 0;
        break;
    case CHIP_ESP32C3:
        count += ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32C3,
                               EFUSE_BIT_DIS_PAD_JTAG_ESP32C3) != 0;
        count += ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C3,
                               EFUSE_MASK_SOFT_DIS_JTAG_ESP32C3) != 0;
        count += ReadEfuseBits(ctx, EFUSE_OFFS_DIS_USB_JTAG_ESP32C3,
                               EFUSE_BIT_DIS_USB_JTAG_ESP32C3) != 0;
        break;
    case CHIP_ESP32C6:
        count += ReadEfuseBits(ctx, EFUSE_OFFS_DIS_PAD_JTAG_ESP32C6,
                               EFUSE_BIT_DIS_PAD_JTAG_ESP32C6) != 0;
        count += ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C6,
                               EFUSE_MASK_SOFT_DIS_JTAG_ESP32C6) != 0;
        count += ReadEfuseBits(ctx, EFUSE_OFFS_DIS_USB_JTAG_ESP32C6,
                               EFUSE_BIT_DIS_USB_JTAG_ESP32C6) != 0;
        break;
    default:
        break;
    }
    return count;
}

/*
 * Efuse_GetJtagTotalCount - Get total number of JTAG interfaces
 */
int Efuse_GetJtagTotalCount(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        return 1;
    case CHIP_ESP32S2:
        return 2;
    case CHIP_ESP32S3:
    case CHIP_ESP32C3:
    case CHIP_ESP32C6:
        return 3;
    default:
        return 0;
    }
}

/*
 * Efuse_GetSoftJtagFlag - Get raw eFuse value for SOFT_DIS_JTAG
 */
DWORD Efuse_GetSoftJtagFlag(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32S2:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S2,
                             EFUSE_BIT_SOFT_DIS_JTAG_ESP32S2) != 0;
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32S3,
                             EFUSE_MASK_SOFT_DIS_JTAG_ESP32S3) >>
               16;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C3,
                             EFUSE_MASK_SOFT_DIS_JTAG_ESP32C3) >>
               16;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_SOFT_DIS_JTAG_ESP32C6,
                             EFUSE_MASK_SOFT_DIS_JTAG_ESP32C6) >>
               16;
    default:
        return 0;
    }
}

/*
 * Efuse_GetUsbJtagFlag - Get raw eFuse value for DIS_USB_JTAG
 */
DWORD Efuse_GetUsbJtagFlag(const CHIP_CTX *ctx)
{
    switch (ctx->type) {
    case CHIP_ESP32S3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_USB_JTAG_ESP32S3,
                             EFUSE_BIT_DIS_USB_JTAG_ESP32S3) != 0;
    case CHIP_ESP32C3:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_USB_JTAG_ESP32C3,
                             EFUSE_BIT_DIS_USB_JTAG_ESP32C3) != 0;
    case CHIP_ESP32C6:
        return ReadEfuseBits(ctx, EFUSE_OFFS_DIS_USB_JTAG_ESP32C6,
                             EFUSE_BIT_DIS_USB_JTAG_ESP32C6) != 0;
    default:
        return 0;
    }
}

/*
 * Efuse_GetKeyPurpose - Get key block purpose from eFuse
 *
 * For S2/S3/C3/C6: reads KEY_PURPOSE_N field from eFuse.
 * For ESP32: hardcoded (BLOCK1=encryption, BLOCK2=secure boot).
 * For C2: hardcoded (BLOCK_KEY0=encryption, only one key block).
 *
 * @ctx:   Pointer to chip context (const, read-only)
 * @block: Key block index (0 = KEY0/KEY_PURPOSE_0, 1 = KEY1, etc.)
 *
 * Returns key purpose value (KEY_PURPOSE_*).
 */
BYTE Efuse_GetKeyPurpose(const CHIP_CTX *ctx, int block)
{
    if (!ctx->efuse || block < 0) {
        return KEY_PURPOSE_USER;
    }

    /* ESP32: fixed key block assignments (no KEY_PURPOSE fields) */
    if (ctx->type == CHIP_ESP32) {
        if (block == 0)
            return KEY_PURPOSE_XTS_AES_128_KEY; /* BLOCK1 = flash encryption */
        return KEY_PURPOSE_USER;
    }

    /* ESP32-C2: only one key block, always flash encryption */
    if (ctx->type == CHIP_ESP32C2) {
        if (block == 0) {
            return KEY_PURPOSE_XTS_AES_128_KEY;
        }
        return KEY_PURPOSE_USER;
    }

    /* S2/S3/C3/C6: read KEY_PURPOSE from eFuse.
       BLOCK0 base varies by chip: ESP32=0x00, S2/S3/C3/C6=0x2C.
       KEY_PURPOSE offsets are relative to BLOCK0 base. */
    if (block > 5) {
        return KEY_PURPOSE_USER;
    }

    static const DWORD purpose_masks[] = {
        EFUSE_MASK_KEY_PURPOSE_0, EFUSE_MASK_KEY_PURPOSE_1,
        EFUSE_MASK_KEY_PURPOSE_2, EFUSE_MASK_KEY_PURPOSE_3,
        EFUSE_MASK_KEY_PURPOSE_4, EFUSE_MASK_KEY_PURPOSE_5,
    };
    static const BYTE purpose_shifts[] = {24, 28, 0, 4, 8, 12};

    /* Chip-specific BLOCK0 base offset in eFuse array */
    int block0_base = (ctx->type == CHIP_ESP32S2 || ctx->type == CHIP_ESP32S3 ||
                       ctx->type == CHIP_ESP32C3 || ctx->type == CHIP_ESP32C6)
                          ? 0x2C
                          : 0x00;

    /* KEY_PURPOSE_N offsets relative to BLOCK0: word2=0x08, word3=0x0C */
    static const BYTE rel_offsets[] = {0x08, 0x08, 0x0C, 0x0C, 0x0C, 0x0C};

    int offset = block0_base + rel_offsets[block];
    DWORD mask = purpose_masks[block];
    int shift = purpose_shifts[block];

    return (BYTE)(ReadEfuseBits(ctx, offset, mask) >> shift);
}

/*
 * Efuse_SetKeyPurpose - Set key block purpose in eFuse
 *
 * For S2/S3/C3/C6: writes KEY_PURPOSE_N field to eFuse.
 * For ESP32/C2: no-op (fixed key assignments).
 *
 * Simulator only: directly modifies eFuse array.
 */
void Efuse_SetKeyPurpose(CHIP_CTX *ctx, int block, BYTE purpose)
{
    if (!ctx->efuse || block < 0 || block > 5) {
        return;
    }

    /* ESP32/C2: fixed purpose, cannot change */
    if (ctx->type == CHIP_ESP32 || ctx->type == CHIP_ESP32C2) {
        return;
    }

    /* ESP32-S3/C3/C6: KEY5 cannot have XTS_AES purposes (hardware bug) */
    if ((ctx->type == CHIP_ESP32S3 || ctx->type == CHIP_ESP32C3 ||
         ctx->type == CHIP_ESP32C6) &&
        block == 5) {
        if (purpose == KEY_PURPOSE_XTS_AES_128_KEY ||
            purpose == KEY_PURPOSE_XTS_AES_256_KEY_1 ||
            purpose == KEY_PURPOSE_XTS_AES_256_KEY_2)
            return;
    }

    static const DWORD purpose_masks[] = {
        EFUSE_MASK_KEY_PURPOSE_0, EFUSE_MASK_KEY_PURPOSE_1,
        EFUSE_MASK_KEY_PURPOSE_2, EFUSE_MASK_KEY_PURPOSE_3,
        EFUSE_MASK_KEY_PURPOSE_4, EFUSE_MASK_KEY_PURPOSE_5,
    };

    /* Chip-specific BLOCK0 base offset in eFuse array */
    int block0_base = (ctx->type == CHIP_ESP32S2 || ctx->type == CHIP_ESP32S3 ||
                       ctx->type == CHIP_ESP32C3 || ctx->type == CHIP_ESP32C6)
                          ? 0x2C
                          : 0x00;
    static const BYTE rel_offsets[] = {0x08, 0x08, 0x0C, 0x0C, 0x0C, 0x0C};

    int offset = block0_base + rel_offsets[block];
    DWORD mask = purpose_masks[block];

    ClearEfuseBits(ctx, offset, mask);
    WriteEfuseBits(ctx, offset, mask, purpose);
}

/*
 * Efuse_GetEncryptionKeyOffset - Get eFuse offset and length of flash encryption
 * key
 *
 * For ESP32: BLOCK1 at offset 0x38 (fixed).
 * For C2: BLOCK_KEY0 at offset 0x60 (fixed).
 * For S2/S3/C3/C6: scans KEY_PURPOSE fields to find XTS_AES key block.
 */
int Efuse_GetEncryptionKeyOffset(const CHIP_CTX *ctx, int *key_len)
{
    if (!key_len) {
        return -1;
    }

    /* ESP32: BLOCK1 at fixed offset (no KEY_PURPOSE fields) */
    if (ctx->type == CHIP_ESP32) {
        *key_len = 32;
        return 0x38;
    }

    /* ESP32-C2: BLOCK_KEY0 at fixed offset (only one key block) */
    if (ctx->type == CHIP_ESP32C2) {
        *key_len = 32;
        return 0x60;
    }

    /* S2/S3/C3/C6: scan KEY_PURPOSE fields to find XTS_AES key block */
    static const DWORD key_block_offsets[] = {0x9C, 0xBC,  0xDC,
                                              0xFC, 0x11C, 0x13C};

    for (int i = 0; i < 6; i++) {
        BYTE purpose = Efuse_GetKeyPurpose(ctx, i);
        if (purpose == KEY_PURPOSE_XTS_AES_128_KEY ||
            purpose == KEY_PURPOSE_XTS_AES_256_KEY_1 ||
            purpose == KEY_PURPOSE_XTS_AES_256_KEY_2) {
            *key_len = 32;
            return (int)key_block_offsets[i];
        }
    }

    /* No encryption key found */
    *key_len = 0;
    return -1;
}

/*
 * Efuse_SetFlashEncryption - Set flash encryption state via eFuse
 *
 * @mode: 0 = no encryption, 1 = dev (encrypted), 2 = release (encrypted + no
 * manual encrypt)
 *
 * Simulator only: directly modifies eFuse array (clears then sets bits).
 */
void Efuse_SetFlashEncryption(CHIP_CTX *ctx, int mode)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        ClearEfuseBits(ctx, 0x00, EFUSE_MASK_FLASH_CRYPT_CNT_ESP32);
        ClearEfuseBits(ctx, 0x18, EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x00, EFUSE_MASK_FLASH_CRYPT_CNT_ESP32, 1);
        }
        if (mode >= 2) {
            WriteEfuseBits(ctx, 0x18, EFUSE_BIT_DISABLE_DL_ENCRYPT_ESP32, 1);
        }
        break;
    case CHIP_ESP32S2:
        ClearEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2);
        ClearEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S2, 1);
        }
        if (mode >= 2)
            WriteEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S2,
                           1);
        break;
    case CHIP_ESP32S3:
        ClearEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3);
        ClearEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32S3, 1);
        }
        if (mode >= 2)
            WriteEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32S3,
                           1);
        break;
    case CHIP_ESP32C2:
        ClearEfuseBits(ctx, 0x30, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2);
        ClearEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x30, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C2, 1);
        }
        if (mode >= 2)
            WriteEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C2,
                           1);
        break;
    case CHIP_ESP32C3:
        ClearEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3);
        ClearEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C3, 1);
        }
        if (mode >= 2)
            WriteEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C3,
                           1);
        break;
    case CHIP_ESP32C6:
        ClearEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6);
        ClearEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x34, EFUSE_MASK_SPI_BOOT_CRYPT_CNT_ESP32C6, 1);
        }
        if (mode >= 2)
            WriteEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DL_MANUAL_ENCRYPT_ESP32C6,
                           1);
        break;
    default:
        break;
    }
}

/*
 * Efuse_SetDownloadMode - Set download mode state via eFuse
 *
 * @mode: 0 = normal, 1 = secure, 2 = disabled
 *
 * Simulator only: directly modifies eFuse array (clears then sets bits).
 */
void Efuse_SetDownloadMode(CHIP_CTX *ctx, int mode)
{
    switch (ctx->type) {
    case CHIP_ESP32:
        ClearEfuseBits(ctx, 0x00, EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32);
        if (mode >= 2) {
            WriteEfuseBits(ctx, 0x00, EFUSE_BIT_UART_DOWNLOAD_DIS_ESP32, 1);
        }
        break;
    case CHIP_ESP32S2:
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2);
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S2, 1);
        }
        if (mode >= 2) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S2, 1);
        }
        break;
    case CHIP_ESP32S3:
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3);
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32S3, 1);
        }
        if (mode >= 2) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32S3, 1);
        }
        break;
    case CHIP_ESP32C2:
        ClearEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2);
        ClearEfuseBits(ctx, 0x30, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x30, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C2, 1);
        }
        if (mode >= 2) {
            WriteEfuseBits(ctx, 0x30, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C2, 1);
        }
        break;
    case CHIP_ESP32C3:
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3);
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C3, 1);
        }
        if (mode >= 2) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C3, 1);
        }
        break;
    case CHIP_ESP32C6:
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6);
        ClearEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6);
        if (mode >= 1) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_ENABLE_SECURITY_DL_ESP32C6, 1);
        }
        if (mode >= 2) {
            WriteEfuseBits(ctx, 0x3C, EFUSE_BIT_DIS_DOWNLOAD_MODE_ESP32C6, 1);
        }
        break;
    default:
        break;
    }
}
