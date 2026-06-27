/*
 * esptool.c - esptool protocol handler implementation
 *
 * Parses SLIP frames, routes commands, and sends responses.
 */

#include "../fesptool_hal.h"
#include "../utils/deflate.h"
#include "../utils/encrypt.h"
#include "fesp.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ESP";

/* Command info structure */
typedef struct {
    const char *name;
} FESP_CMD_INFO;

/* Command table for protocol logging */
static const FESP_CMD_INFO commandTable[256] = {
    [0x02] = {"FLASH_BEGIN"},
    [0x03] = {"FLASH_DATA"},
    [0x04] = {"FLASH_END"},
    [0x05] = {"MEM_BEGIN"},
    [0x06] = {"MEM_END"},
    [0x07] = {"MEM_DATA"},
    [0x08] = {"SYNC"},
    [0x09] = {"WRITE_REG"},
    [0x0A] = {"READ_REG"},
    [0x0B] = {"SPI_SET_PARAMS"},
    [0x0D] = {"SPI_ATTACH"},
    [0x0F] = {"CHANGE_BAUDRATE"},
    [0x10] = {"FLASH_DEFL_BEGIN"},
    [0x11] = {"FLASH_DEFL_DATA"},
    [0x12] = {"FLASH_DEFL_END"},
    [0x13] = {"SPI_FLASH_MD5"},
    [0x14] = {"GET_SECURITY_INFO"},
    [0xD0] = {"ERASE_FLASH"},
    [0xD1] = {"ERASE_REGION"},
    [0xD2] = {"READ_FLASH"},
    [0xD3] = {"RUN_USER_CODE"},
    [0xD5] = {"SPI_NAND_ATTACH"},
    [0xD6] = {"SPI_NAND_READ_SPARE"},
    [0xD7] = {"SPI_NAND_WRITE_SPARE"},
    [0xD8] = {"SPI_NAND_READ_FLASH"},
    [0xD9] = {"SPI_NAND_WRITE_FLASH_BEGIN"},
    [0xDA] = {"SPI_NAND_WRITE_FLASH_DATA"},
    [0xDB] = {"SPI_NAND_ERASE_FLASH"},
    [0xDC] = {"SPI_NAND_ERASE_REGION"},
    [0xDD] = {"SPI_NAND_READ_PAGE_DEBUG"},
    [0xDE] = {"SPI_NAND_WRITE_FLASH_END"},
};

#define ESP_RESP_BUF_SIZE 8192

/* Minimum packet size check macro */
#define CHECK_PKT_SIZE(pkt, min_size)                                          \
    if ((pkt)->size < (min_size)) {                                            \
        FESP_HAL_LOGD(TAG, "Packet too small: cmd=0x%02X size=%u min=%u",      \
                      (pkt)->command, (pkt)->size, (min_size));                \
        return;                                                                \
    }

/* Get command name safely */
static const char *GetCmdName(uint8_t cmd)
{
    const char *name = commandTable[cmd].name;
    return name ? name : "UNKNOWN";
}

/* Little-endian byte readers */
static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* SYNC response Val field prefix: {0x07, 0x07, 0x12}.
   The 4th byte (0x55) comes from request padding, not this array.
   Full response data is all zeros (4-byte status). */
static const uint8_t sync_prefix[3] = {0x07, 0x07, 0x12};

uint8_t fesp_calc_checksum(const uint8_t *data, int len)
{
    uint8_t sum = 0xEF;
    for (int i = 0; i < len; i++) {
        sum ^= data[i];
    }
    return sum;
}

/*
 * defl_free_buffer - Free deflate accumulation buffer (without writing to
 * flash)
 */
static void defl_free_buffer(fesp_ctx_t *ctx)
{
    if (ctx->defl_buf) {
        fesp_hal_mem_free(ctx->defl_buf);
        ctx->defl_buf = NULL;
    }
    ctx->defl_buf_size = 0;
    ctx->defl_buf_cap = 0;
}

/*
 * encrypt_in_place - Encrypt data in-place using flash encryption key
 *
 * @ctx:        Esptool context
 * @data:       Data buffer to encrypt in-place
 * @len:        Data length in bytes
 * @flash_addr: Flash address for tweak calculation
 *
 * Returns FESP_OK on success or if encryption not enabled, FESP_FAIL on error.
 */
static uint32_t encrypt_in_place(fesp_ctx_t *ctx, uint8_t *data, uint32_t len,
                                 uint32_t flash_addr)
{
    if (!ctx->flash_encrypted) {
        return FESP_OK;
    }

    int key_len = 0;
    int key_offset = fesp_efuse_get_encryption_key_offset(ctx->chip, &key_len);

    FESP_HAL_LOGD(TAG,
                  "EncryptInPlace: flash_encrypted=%d key_offset=0x%02X "
                  "key_len=%d efuse=%p efuse_size=%d",
                  ctx->flash_encrypted, key_offset, key_len, ctx->chip->efuse,
                  ctx->chip->efuse_size);

    if (key_offset < 0 || !ctx->chip->efuse ||
        key_offset + key_len > ctx->chip->efuse_size) {
        FESP_HAL_LOGE(TAG, "  No encryption key in eFuse");
        return FESP_FAIL;
    }

    const uint8_t *key = &ctx->chip->efuse[key_offset];
    FESP_HAL_LOGD(TAG,
                  "EncryptInPlace: Key first 8 bytes: %02X %02X %02X %02X %02X "
                  "%02X %02X %02X",
                  key[0], key[1], key[2], key[3], key[4], key[5], key[6],
                  key[7]);

    ENCRYPT_CTX enc_ctx;
    int ret = fesp_hal_encrypt_init(&enc_ctx, key, key_len, flash_addr);
    FESP_HAL_LOGD(TAG,
                  "EncryptInPlace: Encrypt_Init ret=%d flash_addr=0x%08lX "
                  "len=%lu key_len=%d",
                  ret, flash_addr, len, key_len);

    if (ret == ENCRYPT_OK) {
        ret = fesp_hal_encrypt_data(&enc_ctx, data, data, len);
    }

    if (ret == ENCRYPT_OK) {
        FESP_HAL_LOGD(
            TAG,
            "EncryptInPlace: Success, encrypted %lu bytes at offset 0x%08lX",
            len, flash_addr);
        FESP_HAL_LOGD(
            TAG,
            "EncryptInPlace: Output first 8 bytes: %02X %02X %02X %02X "
            "%02X %02X %02X %02X",
            data[0], data[1], data[2], data[3], data[4], data[5], data[6],
            data[7]);
        FESP_HAL_LOGI(TAG, "  Encrypted %lu bytes at offset 0x%08lX", len,
                      flash_addr);
        return FESP_OK;
    }

    FESP_HAL_LOGE(TAG, "  Encryption failed: %d", ret);
    return FESP_FAIL;
}

/*
 * decrypt_in_place - Decrypt data in-place using flash encryption key
 *
 * @ctx:        Esptool context
 * @data:       Data buffer to decrypt in-place
 * @len:        Data length in bytes
 * @flash_addr: Flash address for tweak calculation
 *
 * Returns FESP_OK on success or if decryption not needed, FESP_FAIL on error.
 */
static uint32_t decrypt_in_place(fesp_ctx_t *ctx, uint8_t *data, uint32_t len,
                                 uint32_t flash_addr)
{
    if (!fesp_efuse_is_flash_encryption_enabled(ctx->chip)) {
        return FESP_OK;
    }

    /* ESP32: DISABLE_DL_DECRYPT disables decryption in download mode */
    if (fesp_efuse_is_download_decrypt_disabled(ctx->chip)) {
        FESP_HAL_LOGD(
            TAG,
            "DecryptInPlace: DISABLE_DL_DECRYPT set, returning ciphertext");
        return FESP_OK;
    }

    int key_len = 0;
    int key_offset = fesp_efuse_get_encryption_key_offset(ctx->chip, &key_len);

    if (key_offset < 0 || !ctx->chip->efuse ||
        key_offset + key_len > ctx->chip->efuse_size) {
        FESP_HAL_LOGD(TAG, "DecryptInPlace: No encryption key available");
        return FESP_FAIL;
    }

    const uint8_t *key = &ctx->chip->efuse[key_offset];
    ENCRYPT_CTX enc_ctx;
    int ret = fesp_hal_encrypt_init(&enc_ctx, key, key_len, flash_addr);

    if (ret == ENCRYPT_OK) {
        ret = fesp_hal_decrypt_data(&enc_ctx, data, data, len);
    }

    if (ret == ENCRYPT_OK) {
        FESP_HAL_LOGD(
            TAG,
            "DecryptInPlace: Success, decrypted %lu bytes at offset 0x%08lX",
            len, flash_addr);
        return FESP_OK;
    }

    FESP_HAL_LOGD(TAG, "DecryptInPlace: Decryption failed ret=%d", ret);
    return FESP_FAIL;
}

/* Flush deflate accumulation buffer: decompress and write to flash.
   Returns FESP_OK on success, FESP_FAIL on failure. */
static uint32_t defl_flush_buffer(fesp_ctx_t *ctx)
{
    if (!ctx->defl_buf || ctx->defl_buf_size == 0 || ctx->defl_unc_size == 0) {
        defl_free_buffer(ctx);
        return FESP_OK;
    }

    /* Reuse or allocate decompression buffer */
    if (ctx->decomp_buf && ctx->decomp_buf_cap < ctx->defl_unc_size) {
        fesp_hal_mem_free(ctx->decomp_buf);
        ctx->decomp_buf = NULL;
        ctx->decomp_buf_cap = 0;
    }
    if (!ctx->decomp_buf) {
        ctx->decomp_buf =
            (uint8_t *)fesp_hal_mem_zero_alloc(ctx->defl_unc_size);
        if (!ctx->decomp_buf) {
            FESP_HAL_LOGE(TAG, "  Failed to allocate decompression buffer");
            defl_free_buffer(ctx);
            return FESP_FAIL;
        }
        ctx->decomp_buf_cap = ctx->defl_unc_size;
    }

    /* Initialize decompressor */
    DEFLATE_CTX deflate_ctx;
    fesp_hal_deflate_init(&deflate_ctx, ctx->defl_buf, ctx->defl_buf_size,
                          ctx->decomp_buf, ctx->decomp_buf_cap);

    /* Decompress */
    int ret = fesp_hal_deflate_decompress(&deflate_ctx);
    if (ret != DEFLATE_OK) {
        FESP_HAL_LOGE(TAG, "  Decompression failed: %d", ret);
        defl_free_buffer(ctx);
        return FESP_FAIL;
    }

    uint32_t decomp_size = (uint32_t)deflate_ctx.out_pos;
    FESP_HAL_LOGI(TAG, "  Decompressed %lu -> %lu bytes at offset 0x%08lX",
                  ctx->defl_buf_size, decomp_size, ctx->defl_offset);

    /* Encrypt if encrypted flag was set */
    if (encrypt_in_place(ctx, ctx->decomp_buf, decomp_size, ctx->defl_offset) !=
        FESP_OK) {
        defl_free_buffer(ctx);
        return FESP_FAIL;
    }

    /* Write to flash */
    fesp_flash_write(ctx->flash, ctx->defl_offset, ctx->decomp_buf,
                     decomp_size);

    defl_free_buffer(ctx);
    return FESP_OK;
}

/*
 * fesp_init - Initialize ESP protocol context
 *
 * Binds protocol context to device chip and flash data.
 * Must be called before any other fesp_* function.
 *
 * @ctx:   Pointer to protocol context to initialize
 * @chip:  Pointer to chip context (not owned by esptool)
 * @flash: Pointer to flash context (not owned by esptool)
 */
void fesp_init(fesp_ctx_t *ctx, fesp_chip_ctx_t *chip, fesp_flash_ctx_t *flash)
{
    memset(ctx, 0, sizeof(fesp_ctx_t));
    fesp_slip_init(&ctx->slip);
    ctx->chip = chip;
    ctx->flash = flash;
    ctx->state = FESP_STATE_IDLE;
    ctx->synced = false;
    ctx->stub_mode = false;
    ctx->defl_buf = NULL;
    ctx->defl_buf_size = 0;
    ctx->defl_buf_cap = 0;
    ctx->decomp_buf = NULL;
    ctx->decomp_buf_cap = 0;
}

/*
 * fesp_close - Release persistent resources
 *
 * Frees decompression buffer and any pending deflate buffer.
 * Call when shutting down the protocol handler.
 *
 * @ctx: Pointer to protocol context
 */
void fesp_close(fesp_ctx_t *ctx)
{
    defl_free_buffer(ctx);
    if (ctx->decomp_buf) {
        fesp_hal_mem_free(ctx->decomp_buf);
        ctx->decomp_buf = NULL;
        ctx->decomp_buf_cap = 0;
    }
}

/*
 * fesp_reset_state - Reset protocol state machine
 *
 * Resets protocol state to IDLE, frees pending deflate buffer,
 * and clears all session data. Called on download mode entry.
 *
 * @ctx: Pointer to protocol context
 */
void fesp_reset_state(fesp_ctx_t *ctx)
{
    /* Free deflate buffer without writing (reset scenario) */
    defl_free_buffer(ctx);

    ctx->state = FESP_STATE_IDLE;
    ctx->synced = false;
    ctx->stub_mode = false;
    ctx->flash_offset = 0;
    ctx->flash_seq = 0;
    ctx->last_read_val = 0;
    ctx->flash_uncompressed_size = 0;
    ctx->defl_offset = 0;
    ctx->defl_unc_size = 0;
    ctx->flash_encrypted = false;
    fesp_slip_reset(&ctx->slip);
    FESP_HAL_LOGI(TAG, "  Protocol state reset");
}

/*
 * fesp_send_response_ex - Send protocol response with configurable status
 * length
 *
 * @ctx:        Protocol context
 * @cmd:        Command code (response will echo this)
 * @req_val:    Value field in response (usually last READ_REG value)
 * @status:     Status code (FESP_OK or FESP_FAIL)
 * @status_len: Status length in bytes (2 for stub, 4 for ROM)
 * @data:       Optional data payload (can be NULL)
 * @data_len:   Data payload length
 */
void fesp_send_response_ex(fesp_ctx_t *ctx, uint8_t cmd, uint32_t req_val,
                           uint32_t status, uint8_t status_len,
                           const uint8_t *data, uint16_t data_len)
{
    uint8_t resp[ESP_RESP_BUF_SIZE];
    int pos = 0;

    /* Calculate total size: header(8) + data_len */
    uint16_t total_data_len = data_len;

    FESP_HAL_LOGD(TAG,
                  "SendResponse cmd=0x%02X req_val=0x%08lX status=0x%08lX "
                  "status_len=%u data_len=%u",
                  cmd, req_val, status, status_len, data_len);

    /* Check if data fits in response buffer */
    if (data_len > sizeof(resp) - 8) {
        FESP_HAL_LOGE(TAG, "Response too large: cmd=0x%02X size=%u", cmd,
                      data_len);
        return;
    }

    resp[pos++] = FESP_DIR_RESPONSE;
    resp[pos++] = cmd;
    resp[pos++] = (uint8_t)(total_data_len & 0xFF);
    resp[pos++] = (uint8_t)(total_data_len >> 8);
    resp[pos++] = (uint8_t)(req_val & 0xFF);
    resp[pos++] = (uint8_t)((req_val >> 8) & 0xFF);
    resp[pos++] = (uint8_t)((req_val >> 16) & 0xFF);
    resp[pos++] = (uint8_t)((req_val >> 24) & 0xFF);

    if (data && data_len > 0) {
        memcpy(&resp[pos], data, data_len);
    } else if (data_len > 0) {
        memset(&resp[pos], 0, data_len);
    }
    pos += data_len;

    /* SLIP encoding: worst case each byte needs escaping (2 bytes) + 2 frame
       markers. Use stack buffer for typical responses (<256 bytes encoded). */
    uint8_t encoded_stack[256];
    uint32_t encoded_max = (uint32_t)pos * 2 + 2;
    uint8_t *encoded;
    bool used_heap = false;

    if (encoded_max <= sizeof(encoded_stack)) {
        encoded = encoded_stack;
    } else {
        encoded = (uint8_t *)fesp_hal_mem_alloc(encoded_max);
        if (!encoded) {
            FESP_HAL_LOGD(TAG, "Failed to allocate encoded buffer (%lu bytes)",
                          encoded_max);
            return;
        }
        used_heap = true;
    }

    int enc_len = fesp_slip_encode(resp, pos, encoded, encoded_max);
    if (enc_len > 0) {
        fesp_hal_write(encoded, (uint32_t)enc_len);
    }

    if (used_heap) {
        fesp_hal_mem_free(encoded);
    }

    const char *cmdName = GetCmdName(cmd);
    FESP_HAL_LOGI(TAG, "[RES] %hs size=%u status=0x%08lX", cmdName,
                  total_data_len, status);
}

/*
 * fesp_send_response - Send protocol response with 4-byte status
 *
 * Convenience wrapper for fesp_send_response_ex with status_len=4.
 *
 * @ctx:      Pointer to protocol context
 * @cmd:      Command code (response will echo this)
 * @req_val:  Value field in response
 * @status:   Status code (FESP_OK or FESP_FAIL)
 * @data:     Optional data payload (can be NULL)
 * @data_len: Data payload length
 */
void fesp_send_response(fesp_ctx_t *ctx, uint8_t cmd, uint32_t req_val,
                        uint32_t status, const uint8_t *data, uint16_t data_len)
{
    fesp_send_response_ex(ctx, cmd, req_val, status, 4, data, data_len);
}

/*
 * parse_packet - Parse raw SLIP frame into fesp_packet_t structure
 *
 * Extracts direction, command, size, value, and data fields from frame.
 *
 * @frame:     Pointer to decoded SLIP frame data
 * @frame_len: Length of frame data in bytes
 * @pkt:       Pointer to packet structure to fill
 *
 * Returns true on success, false if frame is too short or malformed.
 */
static bool parse_packet(const uint8_t *frame, int frame_len,
                         fesp_packet_t *pkt)
{
    if (frame_len < 8) {
        return false;
    }

    pkt->direction = frame[0];
    pkt->command = frame[1];
    pkt->size = read_le16(frame + 2);
    pkt->value = read_le32(frame + 4);

    if (pkt->size > 0) {
        if (frame_len < 8 + pkt->size) {
            return false;
        }
        if (pkt->size > sizeof(pkt->data)) {
            return false;
        }
        memcpy(pkt->data, &frame[8], pkt->size);
    }

    return true;
}

/*
 * handle_sync - Handle SYNC command (0x08)
 *
 * Synchronizes with esptool client. Transitions state to SYNCED.
 * Sends 8 consecutive responses as real device does.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet
 */
static void handle_sync(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    FESP_HAL_LOGI(TAG, "  Sync handshake");
    ctx->state = FESP_STATE_SYNCED;
    ctx->synced = true;
    ctx->stub_mode = false;

    /* Real device returns sync sequence in Value field:
       {0x07, 0x07, 0x12, 0x55} as little-endian uint32_t 0x55120707
       Note: The 4th byte is 0x55 (first padding byte from request), not 0x20 */
    uint32_t sync_val =
        ((uint32_t)sync_prefix[0]) | ((uint32_t)sync_prefix[1] << 8) |
        ((uint32_t)sync_prefix[2] << 16) | ((uint32_t)0x55 << 24);

    /* Real device sends 8 consecutive responses per SYNC request.
       Response format: Size=4, Data=4 bytes status (0x00000000) */
    for (int i = 0; i < FESP_SYNC_RESPONSE_COUNT; i++) {
        fesp_send_response_ex(ctx, FESP_CMD_SYNC, sync_val, FESP_OK, 4, NULL,
                              4);
    }
}

/*
 * handle_read_reg - Handle READ_REG command (0x0A)
 *
 * Reads a register value. Transitions to READY when chip detection
 * register (0x40001000) is read.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data = 4-byte address)
 */
static void handle_read_reg(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t addr = read_le32(pkt->data);
    uint32_t val = fesp_chip_read_reg(ctx->chip, addr);

    FESP_HAL_LOGI(TAG, "  addr=0x%08lX -> 0x%08lX", addr, val);

    /* Cache the register value for use in subsequent responses */
    ctx->last_read_val = val;

    /* Transition to READY state when chip detection register is read */
    if (addr == FESP_CHIP_DETECT_REG && ctx->state == FESP_STATE_SYNCED) {
        ctx->state = FESP_STATE_READY;
        FESP_HAL_LOGI(TAG, "  Chip detected, ready for commands");
    }

    /* Real device returns register value in Value field (bytes 4-7),
       with status in Data field */
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_READ_REG, val, FESP_OK, status_len,
                          NULL, status_len);
}

/*
 * handle_write_reg - Handle WRITE_REG command (0x09)
 *
 * Writes a register value with optional mask.
 * Request format: [addr:4][value:4][mask:4][delay_us:4]
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet
 */
static void handle_write_reg(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 8);
    /* WRITE_REG request format (16 bytes = 4 x 32-bit words):
       [addr:4][value:4][mask:4][delay_us:4] */
    uint32_t addr = read_le32(pkt->data);
    uint32_t val = read_le32(pkt->data + 4);
    uint32_t mask = 0xFFFFFFFF;
    uint32_t delayUs = 0;

    if (pkt->size >= 12) {
        mask = read_le32(pkt->data + 8);
    }
    if (pkt->size >= 16) {
        delayUs = read_le32(pkt->data + 12);
    }

    FESP_HAL_LOGI(TAG, "  addr=0x%08lX val=0x%08lX mask=0x%08lX delay=%lu",
                  addr, val, mask, delayUs);

    /* Apply mask: only bits set in mask are written */
    uint32_t currentVal = fesp_chip_read_reg(ctx->chip, addr);
    uint32_t newVal = (currentVal & ~mask) | (val & mask);
    fesp_chip_write_reg(ctx->chip, addr, newVal);

    fesp_hal_modified();
    /* WRITE_REG always returns 2-byte status, Val field is 0x00000000 */
    fesp_send_response_ex(ctx, FESP_CMD_WRITE_REG, 0x00000000, FESP_OK, 2, NULL,
                          2);
}

/*
 * handle_change_baudrate - Handle CHANGE_BAUDRATE command (0x0F)
 *
 * Changes serial port baud rate. Response sent at old rate,
 * then switches to new rate.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data = [new_baud:4][old_baud:4])
 */
static void handle_change_baudrate(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t new_baud = read_le32(pkt->data);
    uint32_t old_baud = 115200;

    if (pkt->size >= 8) {
        old_baud = read_le32(pkt->data + 4);
    }

    FESP_HAL_LOGI(TAG, "  old=%lu new=%lu", old_baud, new_baud);

    /* Send response at old baud rate first */
    /* CHANGE_BAUDRATE always returns 2-byte status */
    fesp_send_response_ex(ctx, FESP_CMD_CHANGE_BAUDRATE, ctx->last_read_val,
                          FESP_OK, 2, NULL, 2);

    /* Then switch to new baud rate */
    fesp_hal_set_baud_rate(new_baud);
    FESP_HAL_LOGI(TAG, "  Baud rate switched to %lu", new_baud);
}

/*
 * handle_mem_begin - Handle MEM_BEGIN command (0x05)
 *
 * Starts memory write session for Stub upload.
 * Transitions state to MEM_WRITING.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data =
 * [total:4][blocks:4][bsize:4][offset:4])
 */
static void handle_mem_begin(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t total = read_le32(pkt->data);
    uint32_t blocks = read_le32(pkt->data + 4);
    uint32_t bsize = read_le32(pkt->data + 8);
    uint32_t offset = read_le32(pkt->data + 12);

    FESP_HAL_LOGI(TAG, "  total=%lu blocks=%lu bsize=%lu offset=0x%08lX", total,
                  blocks, bsize, offset);

    ctx->state = FESP_STATE_MEM_WRITING;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_MEM_BEGIN, ctx->last_read_val, FESP_OK,
                          status_len, NULL, status_len);
}

/*
 * handle_mem_data - Handle MEM_DATA command (0x07)
 *
 * Receives a block of data for Stub upload.
 * Verifies checksum before acknowledging.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet
 */
static void handle_mem_data(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t seq = read_le32(pkt->data);

    FESP_HAL_LOGI(TAG, "  seq=%lu len=%u", seq, pkt->size);

    /* Verify checksum: payload starts at offset 16 */
    if (pkt->size > 16) {
        const uint8_t *payload = &pkt->data[16];
        int payload_len = pkt->size - 16;
        uint8_t expected = fesp_calc_checksum(payload, payload_len);
        uint8_t received = (uint8_t)(pkt->value & 0xFF);
        if (expected != received) {
            FESP_HAL_LOGI(TAG,
                          "  Checksum mismatch: expected=0x%02X "
                          "received=0x%02X",
                          expected, received);
            uint8_t status_len = FESP_STATUS_LEN(ctx);
            fesp_send_response_ex(ctx, FESP_CMD_MEM_DATA, ctx->last_read_val,
                                  FESP_FAIL, status_len, NULL, status_len);
            return;
        }
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_MEM_DATA, ctx->last_read_val, FESP_OK,
                          status_len, NULL, status_len);
}

/*
 * handle_mem_end - Handle MEM_END command (0x06)
 *
 * Completes memory write session. Sends "OHAI" handshake
 * to indicate Stub is ready. Transitions state to READY.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data = [execute:4][entry_point:4])
 */
static void handle_mem_end(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t execute = read_le32(pkt->data);

    FESP_HAL_LOGI(TAG, "  execute=%lu", execute);
    FESP_HAL_LOGD(TAG, "MEM_END: stub_mode before=%d", ctx->stub_mode);

    ctx->state = FESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_MEM_END, ctx->last_read_val, FESP_OK,
                          status_len, NULL, status_len);

    /* Send "OHAI" handshake after MEM_END to indicate stub is ready.
       Real device sends OHAI regardless of execute flag. */
    if (!ctx->stub_mode) {
        uint8_t ohai[] = {0xC0, 'O', 'H', 'A', 'I', 0xC0};
        fesp_hal_write(ohai, sizeof(ohai));
        ctx->stub_mode = true;
        FESP_HAL_LOGI(TAG, "  Stub mode: OHAI sent");
    } else {
        FESP_HAL_LOGD(TAG, "MEM_END: stub_mode already true, skipping OHAI");
    }
}

/*
 * handle_flash_defl_begin - Handle compressed flash write begin command
 *
 * FLASH_DEFL_BEGIN (0x10) starts a compressed flash write session.
 * The command erases the specified flash region and prepares for
 * receiving compressed data blocks.
 *
 * Implementation uses accumulation approach: compressed data is
 * accumulated in a buffer and decompressed all at once when
 * FLASH_DEFL_END is received.
 *
 * Request format: [uncompressed_size:4][num_blocks:4][block_size:4][offset:4]
 */
static void handle_flash_defl_begin(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    /* FLASH_DEFL_BEGIN format:
       [uncompressed_size:4][num_blocks:4][block_size:4][offset:4] */
    uint32_t uncompressed_size = read_le32(pkt->data);
    uint32_t offset = read_le32(pkt->data + 12);

    /* ROM mode sends extra 4 bytes for encrypted flag */
    uint32_t encrypted = 0;
    if (pkt->size >= 20) {
        encrypted = read_le32(pkt->data + 16);
    }

    /* Release mode: reject plaintext writes when encryption is active */
    if (!encrypted && fesp_efuse_is_flash_encryption_enabled(ctx->chip) &&
        fesp_efuse_is_download_encrypt_disabled(ctx->chip)) {
        TAG, FESP_HAL_LOGE(TAG, "  Release mode: plaintext flash disabled");
        uint8_t status_len = FESP_STATUS_LEN(ctx);
        fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_BEGIN,
                              ctx->last_read_val, FESP_FAIL, status_len, NULL,
                              status_len);
        return;
    }

    FESP_HAL_LOGD(TAG, "fesp_chip_type_t=%d stub_mode=%d", encrypted,
                  ctx->chip->type, ctx->stub_mode);
    FESP_HAL_LOGD(TAG, "IsDownloadEncryptDisabled=%d",
                  fesp_efuse_is_flash_encryption_enabled(ctx->chip),
                  fesp_efuse_is_download_encrypt_disabled(ctx->chip));

#if FESP_HAL_LOG_HAS_DEBUG
    /* Log key availability */
    int key_len = 0;
    int key_offset =
        fesp_efuse_get_encryption_key_offset(ctx->chip, &key_len);
    FESP_HAL_LOGD(TAG, "efuse_size=%d", key_offset, key_len,
                    ctx->chip->efuse, ctx->chip->efuse_size);
    if (key_offset >= 0 && ctx->chip->efuse &&
        key_offset + key_len <= ctx->chip->efuse_size) {
        const uint8_t *key = &ctx->chip->efuse[key_offset];
        FESP_HAL_LOGD(TAG, "%02X %02X %02X %02X %02X", key[0], key[1],
                        key[2], key[3], key[4], key[5], key[6], key[7]);
    } else {
        FESP_HAL_LOGD(TAG, "FLASH_DEFL_BEGIN: No encryption key available");
    }
#endif

    /* Flush any pending accumulated data from previous session */
    if (ctx->defl_buf && ctx->defl_buf_size > 0) {
        FESP_HAL_LOGI(TAG, "  Flushing previous compressed data");
        uint32_t ret = defl_flush_buffer(ctx);
        if (ret != FESP_OK) {
            FESP_HAL_LOGE(TAG, "  Failed to flush previous compressed data");
            uint8_t status_len = FESP_STATUS_LEN(ctx);
            fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_BEGIN,
                                  ctx->last_read_val, FESP_FAIL, status_len,
                                  NULL, status_len);
            return;
        }
    }
    /* Note: defl_free_buffer is called inside defl_flush_buffer on both success
     * and failure */

    /* Save deflate session info */
    ctx->defl_offset = offset;
    ctx->defl_unc_size = uncompressed_size;

    /* Update protocol state */
    ctx->flash_offset = offset;
    ctx->flash_seq = 0;
    ctx->flash_uncompressed_size = uncompressed_size;
    ctx->flash_encrypted = (encrypted != 0);
    ctx->state = FESP_STATE_FLASH_WRITING;

    /* Erase the flash region (use uncompressed_size for erase calculation) */
    if (uncompressed_size > 0) {
        fesp_flash_erase(ctx->flash, offset, uncompressed_size);
        fesp_hal_modified();
        FESP_HAL_LOGI(TAG, "  Flash erased: offset=0x%08lX size=%lu", offset,
                      uncompressed_size);
    }

    /* Allocate accumulation buffer */
    if (uncompressed_size > 0) {
        ctx->defl_buf = (uint8_t *)fesp_hal_mem_alloc(uncompressed_size);
        if (!ctx->defl_buf) {
            FESP_HAL_LOGE(TAG, "  Failed to allocate deflate buffer: %lu bytes",
                          uncompressed_size);
            uint8_t status_len = FESP_STATUS_LEN(ctx);
            fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_BEGIN,
                                  ctx->last_read_val, FESP_FAIL, status_len,
                                  NULL, status_len);
            return;
        }
        ctx->defl_buf_cap = uncompressed_size;
        ctx->defl_buf_size = 0;
        FESP_HAL_LOGD(TAG, "FLASH_DEFL_BEGIN: allocated %lu bytes buffer",
                      uncompressed_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    /* Use request's value field (checksum) for response Val, not last_read_val
     */
    fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_BEGIN, pkt->value, FESP_OK,
                          status_len, NULL, status_len);
}

/*
 * handle_flash_defl_data - Handle compressed flash data block
 *
 * FLASH_DEFL_DATA (0x11) receives a block of compressed data.
 * Data is accumulated in the deflate buffer for later decompression.
 *
 * Request format:
 * [data_len:4][seq:4][padding:4][padding:4][compressed_data:data_len]
 *
 * The sequence number must match the expected value, and the checksum
 * (XOR of payload) is verified before accumulating the data.
 */
static void handle_flash_defl_data(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t data_len = read_le32(pkt->data);
    uint32_t seq = read_le32(pkt->data + 4);

    FESP_HAL_LOGI(TAG, "  seq=%lu len=%lu", seq, data_len);

    /* Verify sequence number */
    if (seq != ctx->flash_seq) {
        FESP_HAL_LOGI(TAG, "  Seq mismatch: expected=%lu received=%lu",
                      ctx->flash_seq, seq);
        uint8_t status_len = FESP_STATUS_LEN(ctx);
        fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val,
                              FESP_FAIL, status_len, NULL, status_len);
        return;
    }

    if (pkt->size >= 16 && data_len <= (uint32_t)(pkt->size - 16)) {
        const uint8_t *payload = &pkt->data[16];

        /* Verify checksum */
        uint8_t expected = fesp_calc_checksum(payload, (int)data_len);
        uint8_t received = (uint8_t)(pkt->value & 0xFF);
        if (expected != received) {
            FESP_HAL_LOGI(TAG,
                          "  Checksum mismatch: expected=0x%02X "
                          "received=0x%02X",
                          expected, received);
            uint8_t status_len = FESP_STATUS_LEN(ctx);
            fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_DATA,
                                  ctx->last_read_val, FESP_FAIL, status_len,
                                  NULL, status_len);
            return;
        }

        /* Accumulate compressed data into buffer */
        if (ctx->defl_buf_cap > 0 && data_len > 0) {
            /* Check buffer overflow */
            if (ctx->defl_buf_size + data_len > ctx->defl_buf_cap) {
                FESP_HAL_LOGE(TAG, "  Deflate buffer overflow");
                defl_free_buffer(ctx);
                uint8_t status_len = FESP_STATUS_LEN(ctx);
                fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_DATA,
                                      ctx->last_read_val, FESP_FAIL, status_len,
                                      NULL, status_len);
                return;
            }

            memcpy(ctx->defl_buf + ctx->defl_buf_size, payload, data_len);
            ctx->defl_buf_size += data_len;

            FESP_HAL_LOGI(TAG, "  Accumulated %lu/%lu bytes",
                          ctx->defl_buf_size, ctx->defl_buf_cap);
        }

        ctx->flash_seq = seq + 1;
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val,
                          FESP_OK, status_len, NULL, status_len);
}

/*
 * handle_flash_defl_end - Handle compressed flash write end command
 *
 * FLASH_DEFL_END (0x12) completes a compressed flash write session.
 * Decompresses all accumulated data and writes it to flash.
 *
 * Request format: [reboot:4] (0=don't reboot, 1=reboot)
 */
static void handle_flash_defl_end(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    FESP_HAL_LOGI(TAG, "  End compressed flash download");

    /* Decompress accumulated data and write to flash */
    uint32_t ret = defl_flush_buffer(ctx);
    if (ret != FESP_OK) {
        FESP_HAL_LOGE(TAG, "  Decompression flush failed");
        ctx->state = FESP_STATE_READY;
        uint8_t status_len = FESP_STATUS_LEN(ctx);
        fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_END, ctx->last_read_val,
                              FESP_FAIL, status_len, NULL, status_len);
        return;
    }

    ctx->state = FESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_FLASH_DEFL_END, ctx->last_read_val,
                          FESP_OK, status_len, NULL, status_len);
}

/*
 * handle_read_flash - Handle flash read command (stub-only)
 *
 * READ_FLASH (0xD2) reads data from flash memory.
 *
 * Stub protocol flow:
 *   1. Device sends command ACK (2-byte status in command response)
 *   2. Device sends flash data as separate SLIP frames (block_size each)
 *   3. Host sends 4-byte acknowledgment after each frame (ignored by device)
 *   4. Device sends 16-byte MD5 digest as final SLIP frame
 *
 * Request format: [flash_offset:4][read_len:4][block_size:4][packet_size:4]
 */
static void handle_read_flash(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t addr = read_le32(pkt->data);
    uint32_t len = read_le32(pkt->data + 4);
    uint32_t bsize = read_le32(pkt->data + 8);
    uint32_t psize = read_le32(pkt->data + 12);

    (void)psize;

    FESP_HAL_LOGI(TAG, "  addr=0x%08lX len=%lu bsize=%lu", addr, len, bsize);

    /* Step 1: Send command ACK (2-byte status in command response) */
    fesp_send_response_ex(ctx, FESP_CMD_READ_FLASH, ctx->last_read_val, FESP_OK,
                          2, NULL, 2);

    /* Allocate buffers once before the loop */
    uint8_t *buf = (uint8_t *)fesp_hal_mem_alloc(bsize);
    if (!buf) {
        FESP_HAL_LOGE(TAG, "  Failed to allocate read buffer");
        return;
    }

    uint32_t encoded_max = bsize * 2 + 2;
    uint8_t *encoded = (uint8_t *)fesp_hal_mem_alloc(encoded_max);
    if (!encoded) {
        fesp_hal_mem_free(buf);
        FESP_HAL_LOGE(TAG, "  Failed to allocate encode buffer");
        return;
    }

    /* Step 2: Send flash data as separate SLIP frames */
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t chunk_size = len - offset;
        if (chunk_size > bsize) {
            chunk_size = bsize;
        }

        if (!fesp_flash_read(ctx->flash, addr + offset, buf, chunk_size)) {
            FESP_HAL_LOGE(TAG, "  Flash read failed at offset 0x%08lX",
                          addr + offset);
            break;
        }

        /* Decrypt if flash encryption is enabled */
        decrypt_in_place(ctx, buf, chunk_size, addr + offset);

        /* SLIP-encode the chunk and send as raw frame */
        int enc_len =
            fesp_slip_encode(buf, (int)chunk_size, encoded, (int)encoded_max);
        if (enc_len > 0) {
            fesp_hal_write(encoded, (uint32_t)enc_len);
        }

        offset += chunk_size;
    }

    /* Free buffers after the loop */
    fesp_hal_mem_free(encoded);
    fesp_hal_mem_free(buf);

    /* Step 3: Calculate and send 16-byte MD5 digest as final SLIP frame */
    uint8_t md5[16];
    fesp_flash_calc_md5(ctx->flash, addr, len, md5);

    uint8_t
        md5_encoded[34]; /* 16 bytes * 2 (worst case escaping) + 2 (framing) */
    int md5_enc_len =
        fesp_slip_encode(md5, 16, md5_encoded, sizeof(md5_encoded));
    if (md5_enc_len > 0) {
        fesp_hal_write(md5_encoded, (uint32_t)md5_enc_len);
    }

    FESP_HAL_LOGI(TAG, "  Read complete: %lu bytes", len);
}

/*
 * handle_erase_flash - Handle erase entire flash command (stub-only)
 *
 * ERASE_FLASH (0xD0) erases the entire flash memory.
 * Also frees any pending deflate buffer.
 */
static void handle_erase_flash(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    FESP_HAL_LOGI(TAG, "  Erase entire flash");

    /* Free any pending deflate buffer */
    defl_free_buffer(ctx);

    fesp_flash_erase_all(ctx->flash);
    fesp_hal_modified();
    /* ERASE_FLASH is stub-only, always returns 2-byte status */
    fesp_send_response_ex(ctx, FESP_CMD_ERASE_FLASH, ctx->last_read_val,
                          FESP_OK, 2, NULL, 2);
}

/*
 * handle_erase_block - Handle erase flash region command (stub-only)
 *
 * ERASE_REGION (0xD1) erases a specified flash region.
 * Request format: [offset:4][erase_len:4]
 *
 * Flash erase is sector-aligned (4KB boundaries).
 */
static void handle_erase_block(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 8);
    uint32_t offset = read_le32(pkt->data);
    uint32_t len = read_le32(pkt->data + 4);

    FESP_HAL_LOGI(TAG, "  offset=0x%08lX len=%lu", offset, len);

    /* Free any pending deflate buffer */
    defl_free_buffer(ctx);

    /* ERASE_REGION is stub-only, always returns 2-byte status */
    if (fesp_flash_erase(ctx->flash, offset, len)) {
        fesp_hal_modified();
        fesp_send_response_ex(ctx, FESP_CMD_ERASE_REGION, ctx->last_read_val,
                              FESP_OK, 2, NULL, 2);
    } else {
        fesp_send_response_ex(ctx, FESP_CMD_ERASE_REGION, ctx->last_read_val,
                              FESP_FAIL, 2, NULL, 2);
    }
}

/*
 * handle_flash_md5 - Handle flash MD5 calculation command
 *
 * SPI_FLASH_MD5 (0x13) calculates MD5 hash of a flash region.
 *
 * Request format: [addr:4][len:4][padding:8]
 *
 * Response format differs by mode:
 * - ROM mode:   32-byte ASCII hex MD5 + 2-byte status
 * - Stub mode:  16-byte binary MD5 + 2-byte status
 */
static void handle_flash_md5(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 8);
    uint32_t addr = read_le32(pkt->data);
    uint32_t len = read_le32(pkt->data + 4);

    FESP_HAL_LOGI(TAG, "  addr=0x%08lX len=%lu", addr, len);

    uint8_t md5[16];
    fesp_flash_calc_md5(ctx->flash, addr, len, md5);

    /* Response format: [data:N][status:2]
       ROM mode:   [md5_hex:32][status:2] = 34 bytes
       Stub mode:  [md5_bin:16][status:2] = 18 bytes */
    if (ctx->stub_mode) {
        /* Stub mode: return 16-byte binary MD5 + 2-byte status */
        uint8_t resp[18];
        memcpy(&resp[0], md5, 16);
        resp[16] = 0x00; /* status byte 1 (success) */
        resp[17] = 0x00; /* status byte 2 (success) */
        FESP_HAL_LOGI(TAG, "  MD5 (stub, binary)");
        fesp_send_response_ex(ctx, FESP_CMD_SPI_FLASH_MD5, ctx->last_read_val,
                              FESP_OK, 2, resp, 18);
    } else {
        /* ROM mode: return 32-byte ASCII hex MD5 + 2-byte status */
        uint8_t resp[34];
        for (int i = 0; i < 16; i++) {
            snprintf((char *)&resp[i * 2], 3, "%02x", md5[i]);
        }
        resp[32] = 0x00; /* status byte 1 (success) */
        resp[33] = 0x00; /* status byte 2 (success) */
        FESP_HAL_LOGI(TAG, "  MD5=%.*hs", 32, resp);
        fesp_send_response_ex(ctx, FESP_CMD_SPI_FLASH_MD5, ctx->last_read_val,
                              FESP_OK, 2, resp, 34);
    }
}

/*
 * handle_flash_begin - Handle flash write begin command
 *
 * FLASH_BEGIN (0x02) starts a flash write session.
 * Erases the specified flash region and prepares for receiving data blocks.
 *
 * Request format (stub): [erase_size:4][num_blocks:4][block_size:4][offset:4]
 * Request format (ROM):
 * [erase_size:4][num_blocks:4][block_size:4][offset:4][encrypted:4]
 *
 * Note: Does NOT free deflate buffer, as client may send FLASH_BEGIN
 * before FLASH_DEFL_END in some scenarios.
 */
static void handle_flash_begin(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t erase_size = read_le32(pkt->data);
    uint32_t num_blocks = read_le32(pkt->data + 4);
    uint32_t block_size = read_le32(pkt->data + 8);
    uint32_t offset = read_le32(pkt->data + 12);

    /* ROM mode sends extra 4 bytes for encrypted flag */
    uint32_t encrypted = 0;
    if (pkt->size >= 20) {
        encrypted = read_le32(pkt->data + 16);
    }

    /* Release mode: reject plaintext writes when encryption is active */
    if (!encrypted && fesp_efuse_is_flash_encryption_enabled(ctx->chip) &&
        fesp_efuse_is_download_encrypt_disabled(ctx->chip)) {
        FESP_HAL_LOGE(TAG, "  Release mode: plaintext flash disabled");
        uint8_t status_len = FESP_STATUS_LEN(ctx);
        fesp_send_response_ex(ctx, FESP_CMD_FLASH_BEGIN, ctx->last_read_val,
                              FESP_FAIL, status_len, NULL, status_len);
        return;
    }

    /* Note: Do NOT free deflate buffer here.
       Client may send FLASH_BEGIN before FLASH_DEFL_END.
       The buffer will be flushed by handle_flash_defl_end. */

    ctx->flash_offset = offset;
    ctx->flash_seq = 0;
    ctx->flash_encrypted = (encrypted != 0);
    ctx->state = FESP_STATE_FLASH_WRITING;

    FESP_HAL_LOGI(TAG,
                  "  erase=%lu blocks=%lu bsize=%lu "
                  "offset=0x%08lX encrypted=%lu",
                  erase_size, num_blocks, block_size, offset, encrypted);

    /* Erase the flash region as requested by the host */
    if (erase_size > 0) {
        fesp_flash_erase(ctx->flash, offset, erase_size);
        fesp_hal_modified();
        FESP_HAL_LOGI(TAG, "  Flash erased: offset=0x%08lX size=%lu", offset,
                      erase_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_FLASH_BEGIN, pkt->value, FESP_OK,
                          status_len, NULL, status_len);
}

/*
 * handle_flash_data - Handle flash write data block
 *
 * FLASH_DATA (0x03) receives a block of uncompressed data to write to flash.
 *
 * Request format: [data_len:4][seq:4][padding:4][padding:4][data:data_len]
 *
 * Verifies sequence number and checksum before writing to flash.
 * Flash write uses AND operation to simulate real Flash behavior
 * (bits can only be cleared, not set).
 */
static void handle_flash_data(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t data_len = read_le32(pkt->data);
    uint32_t seq = read_le32(pkt->data + 4);

    FESP_HAL_LOGI(TAG, "  seq=%lu len=%lu", seq, data_len);

    /* Verify sequence number */
    if (seq != ctx->flash_seq) {
        FESP_HAL_LOGI(TAG, "  Seq mismatch: expected=%lu received=%lu",
                      ctx->flash_seq, seq);
        uint8_t status_len = FESP_STATUS_LEN(ctx);
        fesp_send_response_ex(ctx, FESP_CMD_FLASH_DATA, ctx->last_read_val,
                              FESP_FAIL, status_len, NULL, status_len);
        return;
    }

    if (pkt->size >= 16 && data_len <= (uint32_t)(pkt->size - 16)) {
        uint8_t *payload = (uint8_t *)&pkt->data[16];

        /* Verify checksum */
        uint8_t expected = fesp_calc_checksum(payload, (int)data_len);
        uint8_t received = (uint8_t)(pkt->value & 0xFF);
        if (expected != received) {
            FESP_HAL_LOGI(TAG,
                          "  Checksum mismatch: expected=0x%02X "
                          "received=0x%02X",
                          expected, received);
            uint8_t status_len = FESP_STATUS_LEN(ctx);
            fesp_send_response_ex(ctx, FESP_CMD_FLASH_DATA, ctx->last_read_val,
                                  FESP_FAIL, status_len, NULL, status_len);
            return;
        }

        /* Encrypt if encrypted flag was set */
        if (encrypt_in_place(ctx, payload, data_len, ctx->flash_offset) !=
            FESP_OK) {
            FESP_HAL_LOGE(TAG, "  Encryption failed at offset 0x%08lX",
                          ctx->flash_offset);
            uint8_t status_len = FESP_STATUS_LEN(ctx);
            fesp_send_response_ex(ctx, FESP_CMD_FLASH_DATA, ctx->last_read_val,
                                  FESP_FAIL, status_len, NULL, status_len);
            return;
        }
        fesp_flash_write(ctx->flash, ctx->flash_offset, payload, data_len);

        ctx->flash_offset += data_len;
        ctx->flash_seq = seq + 1;
    }

    fesp_hal_modified();
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_FLASH_DATA, ctx->last_read_val, FESP_OK,
                          status_len, NULL, status_len);
}

/*
 * handle_flash_end - Handle flash write end command
 *
 * FLASH_END (0x04) completes a flash write session.
 * Optionally triggers a soft reboot.
 *
 * Request format: [reboot:4] (0=don't reboot, 1=reboot)
 *
 * This command can be received in two scenarios:
 * 1. After normal FLASH_DATA blocks (no deflate buffer)
 * 2. After FLASH_DEFL_DATA blocks when ROM mode doesn't send FLASH_DEFL_END
 *
 * In scenario 2, we need to flush the pending deflate buffer.
 */
static void handle_flash_end(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t reboot = read_le32(pkt->data);

    FESP_HAL_LOGI(TAG, "  reboot=%lu", reboot);

    /* Flush any pending deflate buffer (ROM mode scenario) */
    if (ctx->defl_buf && ctx->defl_buf_size > 0) {
        FESP_HAL_LOGI(TAG, "  Flushing pending compressed data");
        if (defl_flush_buffer(ctx) != FESP_OK) {
            FESP_HAL_LOGE(TAG, "  Failed to flush compressed data");
        }
    }

    ctx->state = FESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_FLASH_END, pkt->value, FESP_OK,
                          status_len, NULL, status_len);
}

/*
 * handle_get_security_info - Handle get security info command
 *
 * GET_SECURITY_INFO (0x14) returns chip security configuration.
 * Response format depends on chip type:
 *
 * - ESP8266/ESP32: Command not supported by ROM. Returns error response
 *   so esptool falls back to magic value detection.
 * - ESP32-S2: Returns 14-byte response (12 payload + 2 status, no chip_id).
 *   esptool parses as ESP32-S2 (chip_id=None), falls back to magic value.
 * - ESP32-S3/C2/C3/C6: Returns 22-byte response (20 payload + 2 status)
 *   with IMAGE_CHIP_ID for chip detection.
 *
 * Response data format: [payload:N][status:2]
 * Status bytes are at the END of the data field.
 */
static void handle_get_security_info(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    FESP_HAL_LOGI(TAG, "  Get security info");

    /* ESP8266/ESP32 ROM and stub do not support GET_SECURITY_INFO.
       Return normal response with failure status (FF 00), matching real device
       behavior. esptool sees status != 0 and falls back to magic value
       detection. */
    if (ctx->chip->type == FESP_CHIP_ESP8266 ||
        ctx->chip->type == FESP_CHIP_ESP32) {
        FESP_HAL_LOGI(TAG, "  Not supported on %hs", ctx->chip->name);
        uint8_t err[2] = {0xFF, 0x00};
        fesp_send_response_ex(ctx, FESP_CMD_GET_SECURITY_INFO,
                              ctx->last_read_val, FESP_FAIL, 2, err, 2);
        return;
    }

    /* ESP32-S2: Return 14-byte response (12 payload + 2 status, no chip_id).
       esptool tries resp_data_len=20 first (fails), then resp_data_len=12
       (succeeds). chip_id will be None, causing get_chip_id() to raise
       FatalError, which triggers fallback to magic value detection. */
    if (ctx->chip->type == FESP_CHIP_ESP32S2) {
        FESP_HAL_LOGI(TAG, "  ESP32-S2: no chip_id in response");
        uint32_t flash_crypt_cnt = fesp_efuse_get_flash_crypt_cnt(ctx->chip);
        FESP_HAL_LOGI(TAG, "  flags=0x%08lX flash_crypt_cnt=%u", 0UL,
                      (unsigned)flash_crypt_cnt);
        uint8_t sec_data[14] = {0};
        /* bytes 0-3:   flags (all zeros) */
        /* byte 4:      flash_crypt_cnt */
        sec_data[4] = (uint8_t)(flash_crypt_cnt & 0xFF);
        /* bytes 5-11:  key_purposes (7 bytes, one per key block KEY0-KEY5 +
         * reserved) */
        for (int i = 0; i < 7; i++) {
            sec_data[5 + i] = fesp_efuse_get_key_purpose(ctx->chip, i);
        }
        /* bytes 12-13: status = success (0x00, 0x00) */
        fesp_send_response(ctx, FESP_CMD_GET_SECURITY_INFO, ctx->last_read_val,
                           FESP_OK, sec_data, 14);
        return;
    }

    /* ESP32 stub, ESP32-S3, C2, C3, C6: Return 22-byte response with
       IMAGE_CHIP_ID.
       [flags:4][flash_crypt_cnt:1][key_purposes:7][chip_id:4][api_version:4][status:2]
       For ESP32 stub, chip_id = EFUSE_CHIP_ID (0x00F01D83). */
    uint32_t chip_id = (ctx->chip->type == FESP_CHIP_ESP32)
                           ? ctx->chip->chip_id
                           : ctx->chip->security_chip_id;
    uint32_t flash_crypt_cnt = fesp_efuse_get_flash_crypt_cnt(ctx->chip);

    uint8_t sec_data[22] = {0};
    /* bytes 0-3:   flags (all zeros) */
    /* byte 4:      flash_crypt_cnt */
    sec_data[4] = (uint8_t)(flash_crypt_cnt & 0xFF);
    /* bytes 5-11:  key_purposes (7 bytes, one per key block KEY0-KEY5 +
     * reserved) */
    for (int i = 0; i < 7; i++) {
        sec_data[5 + i] = fesp_efuse_get_key_purpose(ctx->chip, i);
    }
    /* bytes 12-15: chip_id (IMAGE_CHIP_ID, little-endian) */
    sec_data[12] = (uint8_t)(chip_id & 0xFF);
    sec_data[13] = (uint8_t)((chip_id >> 8) & 0xFF);
    sec_data[14] = (uint8_t)((chip_id >> 16) & 0xFF);
    sec_data[15] = (uint8_t)((chip_id >> 24) & 0xFF);
    /* bytes 16-19: api_version (0) */
    /* bytes 20-21: status = success (0x00, 0x00) */

    FESP_HAL_LOGI(TAG, "  flags=0x%08lX flash_crypt_cnt=%u", 0UL,
                  (unsigned)flash_crypt_cnt);
    FESP_HAL_LOGI(TAG, "  chip_id=%lu (0x%08lX) api_version=%lu", chip_id,
                  chip_id, 0UL);

    /* Transition to READY state when chip detection succeeds via
     * GET_SECURITY_INFO */
    if (ctx->state == FESP_STATE_SYNCED) {
        ctx->state = FESP_STATE_READY;
        FESP_HAL_LOGI(TAG,
                      "  Chip detected via security info, ready for commands");
    }

    fesp_send_response(ctx, FESP_CMD_GET_SECURITY_INFO, ctx->last_read_val,
                       FESP_OK, sec_data, 22);
}

/*
 * handle_spi_attach - Handle SPI flash attach command
 *
 * SPI_ATTACH (0x0D) initializes the SPI flash controller.
 * In the simulator, this is a no-op that always succeeds.
 */
static void handle_spi_attach(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    FESP_HAL_LOGI(TAG, "  Attach SPI flash");

    /* SPI_ATTACH: ROM mode 4-byte status, stub mode 2-byte status */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_SPI_ATTACH, ctx->last_read_val, FESP_OK,
                          status_len, NULL, status_len);
}

/*
 * handle_spi_set_params - Handle SPI flash parameters command
 *
 * SPI_SET_PARAMS (0x0B) configures flash chip parameters.
 *
 * Request format (24 bytes = 6 x uint32 LE):
 *   [fl_id:4][total_size:4][block_size:4][sector_size:4][page_size:4][status_mask:4]
 *
 * ESP8266 ROM does not support this command (returns ROM_INVALID_RECV_MSG).
 * ESP32+ ROM and all stubs support it (returns success).
 *
 * In the simulator, flash parameters are configured separately.
 * This handler logs the parameters and returns success.
 */
static void handle_spi_set_params(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    uint32_t fl_id = 0, total_size = 0, block_size = 0, sector_size = 0,
             page_size = 0, status_mask = 0;

    if (pkt->size >= 24) {
        fl_id = read_le32(pkt->data);
        total_size = read_le32(pkt->data + 4);
        block_size = read_le32(pkt->data + 8);
        sector_size = read_le32(pkt->data + 12);
        page_size = read_le32(pkt->data + 16);
        status_mask = read_le32(pkt->data + 20);
    }

    FESP_HAL_LOGI(
        TAG,
        "  fl_id=0x%08lX total=%lu block=%lu sector=%lu page=%lu mask=0x%08lX",
        fl_id, total_size, block_size, sector_size, page_size, status_mask);

    /* ESP8266 ROM does not support SPI_SET_PARAMS.
       Return ROM_INVALID_RECV_MSG error so esptool falls back gracefully. */
    if (ctx->chip->type == FESP_CHIP_ESP8266 && !ctx->stub_mode) {
        FESP_HAL_LOGI(TAG, "  Not supported on ESP8266 ROM");
        uint8_t err_data[4] = {0x01, 0x05, 0x00, 0x00};
        fesp_send_response(ctx, FESP_CMD_SPI_SET_PARAMS, ctx->last_read_val,
                           FESP_OK, err_data, 4);
        return;
    }

    /* ESP32+ ROM and all stubs: return success.
       ROM mode: 4-byte status; stub mode: 2-byte status. */
    uint8_t status_len = FESP_STATUS_LEN(ctx);
    fesp_send_response_ex(ctx, FESP_CMD_SPI_SET_PARAMS, ctx->last_read_val,
                          FESP_OK, status_len, NULL, status_len);
}

/*
 * handle_run_user_code - Handle soft reset command (stub-only)
 *
 * RUN_USER_CODE (0xD3) triggers a soft reset by jumping to user code.
 * This is a fire-and-forget command - client does not wait for response.
 * After sending response, resets protocol state for next connection.
 */
static void handle_run_user_code(fesp_ctx_t *ctx, const fesp_packet_t *pkt)
{
    FESP_HAL_LOGI(TAG, "  Run user code (soft reset)");

    /* RUN_USER_CODE: stub-only, fire-and-forget (client does not wait for
     * response) */
    fesp_send_response_ex(ctx, FESP_CMD_RUN_USER_CODE, ctx->last_read_val,
                          FESP_OK, 2, NULL, 2);

    /* Reset protocol state for next connection */
    fesp_reset_state(ctx);
}

/*
 * fesp_process_frame - Process a complete SLIP frame
 *
 * Parses the frame into an fesp_packet_t, validates direction (must be
 * request), checks protocol state, and dispatches to the appropriate command
 * handler.
 *
 * @ctx:       Pointer to esptool context
 * @frame:     Pointer to decoded SLIP frame data
 * @frame_len: Length of frame data in bytes
 *
 * Returns true if frame was processed successfully, false on error.
 */
bool fesp_process_frame(fesp_ctx_t *ctx, const uint8_t *frame, int frame_len)
{
    fesp_packet_t *pkt = &ctx->pkt;

    FESP_HAL_LOGD(TAG, "RX frame len=%d", frame_len);

    if (!parse_packet(frame, frame_len, pkt)) {
        FESP_HAL_LOGD(TAG, "Invalid packet");
        return false;
    }

    /* Get command name */
    const char *cmdName = GetCmdName(pkt->command);

    /* Get direction string */
    const char *dirStr = (pkt->direction == FESP_DIR_REQUEST) ? "REQ" : "RES";

    /* Log packet summary */
    FESP_HAL_LOGI(TAG, "[%s] %hs size=%u val=0x%08lX", dirStr, cmdName,
                  pkt->size, pkt->value);

    if (pkt->direction != FESP_DIR_REQUEST) {
        FESP_HAL_LOGD(TAG, "Not a request: 0x%02X", pkt->direction);
        return false;
    }

    /* Download mode disabled: ignore all commands (simulate ROM not entering
     * download mode) */
    if (fesp_efuse_is_download_mode_disabled(ctx->chip)) {
        FESP_HAL_LOGI(TAG, "  Download mode disabled, command ignored");
        return false;
    }

    /* Secure download mode: only allow flash-related commands */
    if (fesp_efuse_is_secure_download_enabled(ctx->chip)) {
        bool allowed = false;
        switch (pkt->command) {
        case FESP_CMD_SYNC:
        case FESP_CMD_READ_REG:
        case FESP_CMD_WRITE_REG:
        case FESP_CMD_SPI_ATTACH:
        case FESP_CMD_CHANGE_BAUDRATE:
        case FESP_CMD_GET_SECURITY_INFO:
        case FESP_CMD_FLASH_BEGIN:
        case FESP_CMD_FLASH_DATA:
        case FESP_CMD_FLASH_END:
        case FESP_CMD_FLASH_DEFL_BEGIN:
        case FESP_CMD_FLASH_DEFL_DATA:
        case FESP_CMD_FLASH_DEFL_END:
        case FESP_CMD_SPI_SET_PARAMS:
        case FESP_CMD_SPI_FLASH_MD5:
            allowed = true;
            break;
        default:
            allowed = false;
            break;
        }
        if (!allowed) {
            FESP_HAL_LOGI(TAG, "  Secure download: command 0x%02X rejected",
                          pkt->command);
            uint8_t status_len = FESP_STATUS_LEN(ctx);
            fesp_send_response_ex(ctx, pkt->command, pkt->value, FESP_FAIL,
                                  status_len, NULL, status_len);
            return false;
        }
    }

    /* State validation: check if command is allowed in current state */
    bool valid = true;
    switch (pkt->command) {
    /* SYNC is always allowed (resets to SYNCED) */
    case FESP_CMD_SYNC:
        break;

    /* Commands allowed in IDLE state (before SYNC) - none except SYNC */
    /* Commands allowed in SYNCED state (after SYNC, before chip detection) */
    case FESP_CMD_READ_REG:
    case FESP_CMD_WRITE_REG:
    case FESP_CMD_SPI_ATTACH:
    case FESP_CMD_CHANGE_BAUDRATE:
    case FESP_CMD_GET_SECURITY_INFO:
        if (ctx->state < FESP_STATE_SYNCED) {
            valid = false;
        }
        break;

    /* Commands allowed in READY state (after chip detection) */
    case FESP_CMD_FLASH_BEGIN:
    case FESP_CMD_FLASH_DEFL_BEGIN:
    case FESP_CMD_MEM_BEGIN:
    case FESP_CMD_SPI_FLASH_MD5:
    case FESP_CMD_SPI_SET_PARAMS:
    case FESP_CMD_ERASE_FLASH:
    case FESP_CMD_ERASE_REGION:
    case FESP_CMD_READ_FLASH:
    case FESP_CMD_RUN_USER_CODE:
        if (ctx->state < FESP_STATE_READY) {
            valid = false;
        }
        break;

    /* Flash data commands require FLASH_WRITING state */
    case FESP_CMD_FLASH_DATA:
    case FESP_CMD_FLASH_END:
    case FESP_CMD_FLASH_DEFL_DATA:
    case FESP_CMD_FLASH_DEFL_END:
        if (ctx->state != FESP_STATE_FLASH_WRITING) {
            valid = false;
        }
        break;

    /* Memory data commands require MEM_WRITING state */
    case FESP_CMD_MEM_DATA:
    case FESP_CMD_MEM_END:
        if (ctx->state != FESP_STATE_MEM_WRITING) {
            valid = false;
        }
        break;

    /* NAND flash commands (0xD5-0xDE) - not implemented in FakeEsptool.
       These commands are stub-only and used by ESP32-S3 etc. for NAND flash.
       Listed here explicitly for documentation; they will return
       ROM_INVALID_RECV_MSG. */
    case FESP_CMD_SPI_NAND_ATTACH:
    case FESP_CMD_SPI_NAND_READ_SPARE:
    case FESP_CMD_SPI_NAND_WRITE_SPARE:
    case FESP_CMD_SPI_NAND_READ_FLASH:
    case FESP_CMD_SPI_NAND_WRITE_FLASH_BEGIN:
    case FESP_CMD_SPI_NAND_WRITE_FLASH_DATA:
    case FESP_CMD_SPI_NAND_ERASE_FLASH:
    case FESP_CMD_SPI_NAND_ERASE_REGION:
    case FESP_CMD_SPI_NAND_READ_PAGE_DEBUG:
    case FESP_CMD_SPI_NAND_WRITE_FLASH_END:
        /* Fall through to default - not implemented */

    default:
        valid = false;
        break;
    }

    if (!valid) {
        FESP_HAL_LOGI(TAG, "  Command 0x%02X rejected (state=%d)", pkt->command,
                      ctx->state);
        uint8_t status_len = FESP_STATUS_LEN(ctx);
        fesp_send_response_ex(ctx, pkt->command, pkt->value, FESP_FAIL,
                              status_len, NULL, status_len);
        return false;
    }

    switch (pkt->command) {
    case FESP_CMD_SYNC:
        handle_sync(ctx, pkt);
        break;
    case FESP_CMD_READ_REG:
        handle_read_reg(ctx, pkt);
        break;
    case FESP_CMD_WRITE_REG:
        handle_write_reg(ctx, pkt);
        break;
    case FESP_CMD_SPI_SET_PARAMS:
        handle_spi_set_params(ctx, pkt);
        break;
    case FESP_CMD_SPI_ATTACH:
        handle_spi_attach(ctx, pkt);
        break;
    case FESP_CMD_CHANGE_BAUDRATE:
        handle_change_baudrate(ctx, pkt);
        break;
    case FESP_CMD_FLASH_BEGIN:
        handle_flash_begin(ctx, pkt);
        break;
    case FESP_CMD_FLASH_DATA:
        handle_flash_data(ctx, pkt);
        break;
    case FESP_CMD_FLASH_END:
        handle_flash_end(ctx, pkt);
        break;
    case FESP_CMD_MEM_BEGIN:
        handle_mem_begin(ctx, pkt);
        break;
    case FESP_CMD_MEM_DATA:
        handle_mem_data(ctx, pkt);
        break;
    case FESP_CMD_MEM_END:
        handle_mem_end(ctx, pkt);
        break;
    case FESP_CMD_FLASH_DEFL_BEGIN:
        handle_flash_defl_begin(ctx, pkt);
        break;
    case FESP_CMD_FLASH_DEFL_DATA:
        handle_flash_defl_data(ctx, pkt);
        break;
    case FESP_CMD_FLASH_DEFL_END:
        handle_flash_defl_end(ctx, pkt);
        break;
    case FESP_CMD_SPI_FLASH_MD5:
        handle_flash_md5(ctx, pkt);
        break;
    case FESP_CMD_ERASE_FLASH:
        handle_erase_flash(ctx, pkt);
        break;
    case FESP_CMD_ERASE_REGION:
        handle_erase_block(ctx, pkt);
        break;
    case FESP_CMD_READ_FLASH:
        handle_read_flash(ctx, pkt);
        break;
    case FESP_CMD_GET_SECURITY_INFO:
        handle_get_security_info(ctx, pkt);
        break;
    case FESP_CMD_RUN_USER_CODE:
        handle_run_user_code(ctx, pkt);
        break;
    default:
        /* ROM returns ROM_INVALID_RECV_MSG (0x05) for unsupported commands */
        FESP_HAL_LOGI(TAG, "  Unknown command: 0x%02X", pkt->command);
        /* Response format: [status_byte_1 != 0][ROM_INVALID_RECV_MSG] +
            * padding */
        uint8_t err_data[4] = {0x01, 0x05, 0x00, 0x00};
        uint8_t status_len = FESP_STATUS_LEN(ctx);
        fesp_send_response_ex(ctx, pkt->command, pkt->value, FESP_OK,
                                status_len, err_data, 4);
        return false;
    }

    return true;
}

/*
 * fesp_feed - Feed raw serial data to protocol decoder
 *
 * Processes incoming bytes through SLIP decoder. When a complete
 * frame is assembled, dispatches to fesp_process_frame.
 *
 * @ctx: Pointer to protocol context
 * @data: Pointer to raw serial data
 * @len:  Number of bytes in data
 *
 * Returns true if at least one complete frame was processed.
 */
bool fesp_feed(fesp_ctx_t *ctx, const uint8_t *data, int len)
{
    bool got_frame = false;

    for (int i = 0; i < len; i++) {
        if (fesp_slip_put_byte(&ctx->slip, data[i])) {
            const uint8_t *payload = fesp_slip_get_payload(&ctx->slip);
            int plen = fesp_slip_get_length(&ctx->slip);

            if (plen >= 8) {
                fesp_process_frame(ctx, payload, plen);
                got_frame = true;
            }

            fesp_slip_reset(&ctx->slip);
        }
    }

    return got_frame;
}
