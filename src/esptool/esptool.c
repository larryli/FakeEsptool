/*
 * esptool.c - esptool protocol handler implementation
 *
 * Parses SLIP frames, routes commands, and sends responses.
 */

#include "../esptool_hal.h"
#include "../utils/deflate.h"
#include "../utils/encrypt.h"
#include "esptool.h"
#include <stdio.h>
#include <string.h>

#if ENABLE_TRACE
static const char *TAG = "ESP";
#endif

/* Command info structure */
typedef struct {
    const char *name;
} ESP_CMD_INFO;

/* Command table for protocol logging */
static const ESP_CMD_INFO commandTable[256] = {
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
        EsptoolHal_LogD(TAG, "Packet too small: cmd=0x%02X size=%u min=%u",        \
                    (pkt)->command, (pkt)->size, (min_size));                  \
        return;                                                                \
    }

/* Get command name safely */
static const char *GetCmdName(uint8_t cmd)
{
    const char *name = commandTable[cmd].name;
    return name ? name : "UNKNOWN";
}

/* Little-endian byte readers */
static inline uint16_t ReadLE16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t ReadLE32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* SYNC response Val field prefix: {0x07, 0x07, 0x12}.
   The 4th byte (0x55) comes from request padding, not this array.
   Full response data is all zeros (4-byte status). */
static const uint8_t sync_prefix[3] = {0x07, 0x07, 0x12};

uint8_t Esptool_CalcChecksum(const uint8_t *data, int len)
{
    uint8_t sum = 0xEF;
    for (int i = 0; i < len; i++) {
        sum ^= data[i];
    }
    return sum;
}

/*
 * Defl_FreeBuffer - Free deflate accumulation buffer (without writing to flash)
 */
static void Defl_FreeBuffer(ESPTOOL_CTX *ctx)
{
    if (ctx->defl_buf) {
        EsptoolHal_MemFree(ctx->defl_buf);
        ctx->defl_buf = NULL;
    }
    ctx->defl_buf_size = 0;
    ctx->defl_buf_cap = 0;
}

/*
 * Esptool_EncryptInPlace - Encrypt data in-place using flash encryption key
 *
 * @ctx:        Esptool context
 * @data:       Data buffer to encrypt in-place
 * @len:        Data length in bytes
 * @flash_addr: Flash address for tweak calculation
 *
 * Returns ESP_OK on success or if encryption not enabled, ESP_FAIL on error.
 */
static uint32_t Esptool_EncryptInPlace(ESPTOOL_CTX *ctx, uint8_t *data, uint32_t len,
                                    uint32_t flash_addr)
{
    if (!ctx->flash_encrypted) {
        return ESP_OK;
    }

    int key_len = 0;
    int key_offset = Efuse_GetEncryptionKeyOffset(ctx->chip, &key_len);

    EsptoolHal_LogD(TAG,
                "EncryptInPlace: flash_encrypted=%d key_offset=0x%02X "
                "key_len=%d efuse=%p efuse_size=%d",
                ctx->flash_encrypted, key_offset, key_len, ctx->chip->efuse,
                ctx->chip->efuse_size);

    if (key_offset < 0 || !ctx->chip->efuse ||
        key_offset + key_len > ctx->chip->efuse_size) {
        EsptoolHal_LogD(TAG, "EncryptInPlace: No encryption key available");
        EsptoolHal_LogE("ERR", "  No encryption key in eFuse");
        return ESP_FAIL;
    }

    const uint8_t *key = &ctx->chip->efuse[key_offset];
    EsptoolHal_LogD(TAG,
                "EncryptInPlace: Key first 8 bytes: %02X %02X %02X %02X %02X "
                "%02X %02X %02X",
                key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7]);

    ENCRYPT_CTX enc_ctx;
    int ret = EsptoolHal_EncryptInit(&enc_ctx, key, key_len, flash_addr);
    EsptoolHal_LogD(TAG,
                "EncryptInPlace: Encrypt_Init ret=%d flash_addr=0x%08lX "
                "len=%lu key_len=%d",
                ret, flash_addr, len, key_len);

    if (ret == ENCRYPT_OK) {
        ret = EsptoolHal_EncryptData(&enc_ctx, data, data, len);
    }

    if (ret == ENCRYPT_OK) {
        EsptoolHal_LogD(
            TAG,
            "EncryptInPlace: Success, encrypted %lu bytes at offset 0x%08lX",
            len, flash_addr);
        EsptoolHal_LogD(TAG,
                    "EncryptInPlace: Output first 8 bytes: %02X %02X %02X %02X "
                    "%02X %02X %02X %02X",
                    data[0], data[1], data[2], data[3], data[4], data[5],
                    data[6], data[7]);
        EsptoolHal_LogI("ESP",
                        "  Encrypted %lu bytes at offset 0x%08lX", len,
                        flash_addr);
        return ESP_OK;
    }

    EsptoolHal_LogD(TAG, "EncryptInPlace: Encryption failed ret=%d", ret);
    EsptoolHal_LogE("ERR", "  Encryption failed: %d", ret);
    return ESP_FAIL;
}

/*
 * Esptool_DecryptInPlace - Decrypt data in-place using flash encryption key
 *
 * @ctx:        Esptool context
 * @data:       Data buffer to decrypt in-place
 * @len:        Data length in bytes
 * @flash_addr: Flash address for tweak calculation
 *
 * Returns ESP_OK on success or if decryption not needed, ESP_FAIL on error.
 */
static uint32_t Esptool_DecryptInPlace(ESPTOOL_CTX *ctx, uint8_t *data, uint32_t len,
                                    uint32_t flash_addr)
{
    if (!Efuse_IsFlashEncryptionEnabled(ctx->chip)) {
        return ESP_OK;
    }

    /* ESP32: DISABLE_DL_DECRYPT disables decryption in download mode */
    if (Efuse_IsDownloadDecryptDisabled(ctx->chip)) {
        EsptoolHal_LogD(
            TAG,
            "DecryptInPlace: DISABLE_DL_DECRYPT set, returning ciphertext");
        return ESP_OK;
    }

    int key_len = 0;
    int key_offset = Efuse_GetEncryptionKeyOffset(ctx->chip, &key_len);

    if (key_offset < 0 || !ctx->chip->efuse ||
        key_offset + key_len > ctx->chip->efuse_size) {
        EsptoolHal_LogD(TAG, "DecryptInPlace: No encryption key available");
        return ESP_FAIL;
    }

    const uint8_t *key = &ctx->chip->efuse[key_offset];
    ENCRYPT_CTX enc_ctx;
    int ret = EsptoolHal_EncryptInit(&enc_ctx, key, key_len, flash_addr);

    if (ret == ENCRYPT_OK) {
        ret = EsptoolHal_DecryptData(&enc_ctx, data, data, len);
    }

    if (ret == ENCRYPT_OK) {
        EsptoolHal_LogD(
            TAG,
            "DecryptInPlace: Success, decrypted %lu bytes at offset 0x%08lX",
            len, flash_addr);
        return ESP_OK;
    }

    EsptoolHal_LogD(TAG, "DecryptInPlace: Decryption failed ret=%d", ret);
    return ESP_FAIL;
}

/* Flush deflate accumulation buffer: decompress and write to flash.
   Returns ESP_OK on success, ESP_FAIL on failure. */
static uint32_t Defl_FlushBuffer(ESPTOOL_CTX *ctx)
{
    if (!ctx->defl_buf || ctx->defl_buf_size == 0 || ctx->defl_unc_size == 0) {
        Defl_FreeBuffer(ctx);
        return ESP_OK;
    }

    /* Reuse or allocate decompression buffer */
    if (ctx->decomp_buf && ctx->decomp_buf_cap < ctx->defl_unc_size) {
        EsptoolHal_MemFree(ctx->decomp_buf);
        ctx->decomp_buf = NULL;
        ctx->decomp_buf_cap = 0;
    }
    if (!ctx->decomp_buf) {
        ctx->decomp_buf = (uint8_t *)EsptoolHal_MemZeroAlloc(ctx->defl_unc_size);
        if (!ctx->decomp_buf) {
            EsptoolHal_LogD(TAG, "Failed to allocate decompression buffer");
            EsptoolHal_LogE("ERR",
                           "  Failed to allocate decompression buffer");
            Defl_FreeBuffer(ctx);
            return ESP_FAIL;
        }
        ctx->decomp_buf_cap = ctx->defl_unc_size;
    }

    /* Initialize decompressor */
    DEFLATE_CTX deflate_ctx;
    EsptoolHal_DeflateInit(&deflate_ctx, ctx->defl_buf, ctx->defl_buf_size,
                 ctx->decomp_buf, ctx->decomp_buf_cap);

    /* Decompress */
    int ret = EsptoolHal_DeflateDecompress(&deflate_ctx);
    if (ret != DEFLATE_OK) {
        EsptoolHal_LogD(TAG, "Decompression failed: %d", ret);
        EsptoolHal_LogE("ERR", "  Decompression failed: %d",
                        ret);
        Defl_FreeBuffer(ctx);
        return ESP_FAIL;
    }

    uint32_t decomp_size = (uint32_t)deflate_ctx.out_pos;
    EsptoolHal_LogD(TAG, "Defl flush: %lu -> %lu bytes at offset 0x%08lX",
                ctx->defl_buf_size, decomp_size, ctx->defl_offset);
    EsptoolHal_LogI("ESP",
                    "  Decompressed %lu -> %lu bytes at offset 0x%08lX",
                    ctx->defl_buf_size, decomp_size, ctx->defl_offset);

    /* Encrypt if encrypted flag was set */
    if (Esptool_EncryptInPlace(ctx, ctx->decomp_buf, decomp_size,
                               ctx->defl_offset) != ESP_OK) {
        Defl_FreeBuffer(ctx);
        return ESP_FAIL;
    }

    /* Write to flash */
    Flash_Write(ctx->flash, ctx->defl_offset, ctx->decomp_buf, decomp_size);

    Defl_FreeBuffer(ctx);
    return ESP_OK;
}

/*
 * Esptool_Init - Initialize ESP protocol context
 *
 * Binds protocol context to device chip and flash data.
 * Must be called before any other Esptool_* function.
 *
 * @ctx:   Pointer to protocol context to initialize
 * @chip:  Pointer to chip context (not owned by esptool)
 * @flash: Pointer to flash context (not owned by esptool)
 */
void Esptool_Init(ESPTOOL_CTX *ctx, CHIP_CTX *chip, FLASH_CTX *flash)
{
    memset(ctx, 0, sizeof(ESPTOOL_CTX));
    Slip_Init(&ctx->slip);
    ctx->chip = chip;
    ctx->flash = flash;
    ctx->state = ESP_STATE_IDLE;
    ctx->synced = false;
    ctx->stub_mode = false;
    ctx->defl_buf = NULL;
    ctx->defl_buf_size = 0;
    ctx->defl_buf_cap = 0;
    ctx->decomp_buf = NULL;
    ctx->decomp_buf_cap = 0;
}

/*
 * Esptool_Close - Release persistent resources
 *
 * Frees decompression buffer and any pending deflate buffer.
 * Call when shutting down the protocol handler.
 *
 * @ctx: Pointer to protocol context
 */
void Esptool_Close(ESPTOOL_CTX *ctx)
{
    Defl_FreeBuffer(ctx);
    if (ctx->decomp_buf) {
        EsptoolHal_MemFree(ctx->decomp_buf);
        ctx->decomp_buf = NULL;
        ctx->decomp_buf_cap = 0;
    }
}

/*
 * Esptool_ResetState - Reset protocol state machine
 *
 * Resets protocol state to IDLE, frees pending deflate buffer,
 * and clears all session data. Called on download mode entry.
 *
 * @ctx: Pointer to protocol context
 */
void Esptool_ResetState(ESPTOOL_CTX *ctx)
{
    /* Free deflate buffer without writing (reset scenario) */
    Defl_FreeBuffer(ctx);

    ctx->state = ESP_STATE_IDLE;
    ctx->synced = false;
    ctx->stub_mode = false;
    ctx->flash_offset = 0;
    ctx->flash_seq = 0;
    ctx->last_read_val = 0;
    ctx->flash_uncompressed_size = 0;
    ctx->defl_offset = 0;
    ctx->defl_unc_size = 0;
    ctx->flash_encrypted = false;
    Slip_Reset(&ctx->slip);
    EsptoolHal_LogD(TAG, "Protocol state reset to IDLE");
    EsptoolHal_LogI("ESP", "  Protocol state reset");
}





/*
 * Esptool_SendResponseEx - Send protocol response with configurable status
 * length
 *
 * @ctx:        Protocol context
 * @cmd:        Command code (response will echo this)
 * @req_val:    Value field in response (usually last READ_REG value)
 * @status:     Status code (ESP_OK or ESP_FAIL)
 * @status_len: Status length in bytes (2 for stub, 4 for ROM)
 * @data:       Optional data payload (can be NULL)
 * @data_len:   Data payload length
 */
void Esptool_SendResponseEx(ESPTOOL_CTX *ctx, uint8_t cmd, uint32_t req_val,
                            uint32_t status, uint8_t status_len, const uint8_t *data,
                            uint16_t data_len)
{
    uint8_t resp[ESP_RESP_BUF_SIZE];
    int pos = 0;

    /* Calculate total size: header(8) + data_len */
    uint16_t total_data_len = data_len;

    EsptoolHal_LogD(TAG,
                "SendResponse cmd=0x%02X req_val=0x%08lX status=0x%08lX "
                "status_len=%u data_len=%u",
                cmd, req_val, status, status_len, data_len);

    /* Check if data fits in response buffer */
    if (data_len > sizeof(resp) - 8) {
        EsptoolHal_LogD(TAG, "Response too large: cmd=0x%02X data_len=%u max=%zu",
                    cmd, data_len, sizeof(resp) - 8);
        EsptoolHal_LogE("ERR",
                        "Response too large: cmd=0x%02X size=%u", cmd,
                        data_len);
        return;
    }

    resp[pos++] = ESP_DIR_RESPONSE;
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
        encoded = (uint8_t *)EsptoolHal_MemAlloc(encoded_max);
        if (!encoded) {
            EsptoolHal_LogD(TAG, "Failed to allocate encoded buffer (%lu bytes)",
                        encoded_max);
            return;
        }
        used_heap = true;
    }

    int enc_len = Slip_Encode(resp, pos, encoded, encoded_max);
    if (enc_len > 0) {
        EsptoolHal_Write(encoded, (uint32_t)enc_len);
    }

    if (used_heap) {
        EsptoolHal_MemFree(encoded);
    }

    const char *cmdName = GetCmdName(cmd);
    EsptoolHal_LogI("ESP", "[RES] %hs size=%u status=0x%08lX",
                    cmdName, total_data_len, status);

    EsptoolHal_LogD(TAG, "TX cmd=0x%02X status=%lu len=%u", cmd, status,
                total_data_len);
}

/*
 * Esptool_SendResponse - Send protocol response with 4-byte status
 *
 * Convenience wrapper for Esptool_SendResponseEx with status_len=4.
 *
 * @ctx:      Pointer to protocol context
 * @cmd:      Command code (response will echo this)
 * @req_val:  Value field in response
 * @status:   Status code (ESP_OK or ESP_FAIL)
 * @data:     Optional data payload (can be NULL)
 * @data_len: Data payload length
 */
void Esptool_SendResponse(ESPTOOL_CTX *ctx, uint8_t cmd, uint32_t req_val,
                          uint32_t status, const uint8_t *data, uint16_t data_len)
{
    Esptool_SendResponseEx(ctx, cmd, req_val, status, 4, data, data_len);
}

/*
 * ParsePacket - Parse raw SLIP frame into ESP_PACKET structure
 *
 * Extracts direction, command, size, value, and data fields from frame.
 *
 * @frame:     Pointer to decoded SLIP frame data
 * @frame_len: Length of frame data in bytes
 * @pkt:       Pointer to packet structure to fill
 *
 * Returns true on success, false if frame is too short or malformed.
 */
static bool ParsePacket(const uint8_t *frame, int frame_len, ESP_PACKET *pkt)
{
    if (frame_len < 8) {
        return false;
    }

    pkt->direction = frame[0];
    pkt->command = frame[1];
    pkt->size = ReadLE16(frame + 2);
    pkt->value = ReadLE32(frame + 4);

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
 * HandleSync - Handle SYNC command (0x08)
 *
 * Synchronizes with esptool client. Transitions state to SYNCED.
 * Sends 8 consecutive responses as real device does.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet
 */
static void HandleSync(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    EsptoolHal_LogD(TAG, "SYNC received");
    EsptoolHal_LogI("ESP", "  Sync handshake");
    ctx->state = ESP_STATE_SYNCED;
    ctx->synced = true;
    ctx->stub_mode = false;

    /* Real device returns sync sequence in Value field:
       {0x07, 0x07, 0x12, 0x55} as little-endian uint32_t 0x55120707
       Note: The 4th byte is 0x55 (first padding byte from request), not 0x20 */
    uint32_t sync_val = ((uint32_t)sync_prefix[0]) | ((uint32_t)sync_prefix[1] << 8) |
                     ((uint32_t)sync_prefix[2] << 16) | ((uint32_t)0x55 << 24);

    /* Real device sends 8 consecutive responses per SYNC request.
       Response format: Size=4, Data=4 bytes status (0x00000000) */
    for (int i = 0; i < ESP_SYNC_RESPONSE_COUNT; i++) {
        Esptool_SendResponseEx(ctx, ESP_CMD_SYNC, sync_val, ESP_OK, 4, NULL, 4);
    }
}

/*
 * HandleReadReg - Handle READ_REG command (0x0A)
 *
 * Reads a register value. Transitions to READY when chip detection
 * register (0x40001000) is read.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data = 4-byte address)
 */
static void HandleReadReg(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t addr = ReadLE32(pkt->data);
    uint32_t val = Chip_ReadReg(ctx->chip, addr);

    EsptoolHal_LogD(TAG, "READ_REG addr=0x%08lX val=0x%08lX", addr, val);
    EsptoolHal_LogI("ESP", "  addr=0x%08lX -> 0x%08lX", addr,
                    val);

    /* Cache the register value for use in subsequent responses */
    ctx->last_read_val = val;

    /* Transition to READY state when chip detection register is read */
    if (addr == CHIP_DETECT_REG && ctx->state == ESP_STATE_SYNCED) {
        ctx->state = ESP_STATE_READY;
        EsptoolHal_LogI("ESP",
                       "  Chip detected, ready for commands");
    }

    /* Real device returns register value in Value field (bytes 4-7),
       with status in Data field */
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_READ_REG, val, ESP_OK, status_len, NULL,
                           status_len);
}

/*
 * HandleWriteReg - Handle WRITE_REG command (0x09)
 *
 * Writes a register value with optional mask.
 * Request format: [addr:4][value:4][mask:4][delay_us:4]
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet
 */
static void HandleWriteReg(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 8);
    /* WRITE_REG request format (16 bytes = 4 x 32-bit words):
       [addr:4][value:4][mask:4][delay_us:4] */
    uint32_t addr = ReadLE32(pkt->data);
    uint32_t val = ReadLE32(pkt->data + 4);
    uint32_t mask = 0xFFFFFFFF;
    uint32_t delayUs = 0;

    if (pkt->size >= 12) {
        mask = ReadLE32(pkt->data + 8);
    }
    if (pkt->size >= 16) {
        delayUs = ReadLE32(pkt->data + 12);
    }

    EsptoolHal_LogD(TAG,
                "WRITE_REG addr=0x%08lX val=0x%08lX "
                "mask=0x%08lX delay=%lu",
                addr, val, mask, delayUs);
    EsptoolHal_LogI("ESP",
                    "  addr=0x%08lX val=0x%08lX mask=0x%08lX delay=%lu", addr,
                    val, mask, delayUs);

    /* Apply mask: only bits set in mask are written */
    uint32_t currentVal = Chip_ReadReg(ctx->chip, addr);
    uint32_t newVal = (currentVal & ~mask) | (val & mask);
    Chip_WriteReg(ctx->chip, addr, newVal);

    EsptoolHal_Modified();
    /* WRITE_REG always returns 2-byte status, Val field is 0x00000000 */
    Esptool_SendResponseEx(ctx, ESP_CMD_WRITE_REG, 0x00000000, ESP_OK, 2, NULL,
                           2);
}

/*
 * HandleChangeBaudrate - Handle CHANGE_BAUDRATE command (0x0F)
 *
 * Changes serial port baud rate. Response sent at old rate,
 * then switches to new rate.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data = [new_baud:4][old_baud:4])
 */
static void HandleChangeBaudrate(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t new_baud = ReadLE32(pkt->data);
    uint32_t old_baud = 115200;

    if (pkt->size >= 8) {
        old_baud = ReadLE32(pkt->data + 4);
    }

    EsptoolHal_LogD(TAG, "CHANGE_BAUDRATE old=%lu new=%lu", old_baud, new_baud);
    EsptoolHal_LogI("ESP", "  old=%lu new=%lu", old_baud,
                    new_baud);

    /* Send response at old baud rate first */
    /* CHANGE_BAUDRATE always returns 2-byte status */
    Esptool_SendResponseEx(ctx, ESP_CMD_CHANGE_BAUDRATE, ctx->last_read_val,
                           ESP_OK, 2, NULL, 2);

    /* Then switch to new baud rate */
    EsptoolHal_SetBaudRate(new_baud);
    EsptoolHal_LogI("ESP", "  Baud rate switched to %lu",
                    new_baud);
}

/*
 * HandleMemBegin - Handle MEM_BEGIN command (0x05)
 *
 * Starts memory write session for Stub upload.
 * Transitions state to MEM_WRITING.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data =
 * [total:4][blocks:4][bsize:4][offset:4])
 */
static void HandleMemBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t total = ReadLE32(pkt->data);
    uint32_t blocks = ReadLE32(pkt->data + 4);
    uint32_t bsize = ReadLE32(pkt->data + 8);
    uint32_t offset = ReadLE32(pkt->data + 12);

    EsptoolHal_LogD(TAG, "MEM_BEGIN total=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                total, blocks, bsize, offset);
    EsptoolHal_LogI("ESP",
                    "  total=%lu blocks=%lu bsize=%lu offset=0x%08lX", total,
                    blocks, bsize, offset);

    ctx->state = ESP_STATE_MEM_WRITING;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_MEM_BEGIN, ctx->last_read_val, ESP_OK,
                           status_len, NULL, status_len);
}

/*
 * HandleMemData - Handle MEM_DATA command (0x07)
 *
 * Receives a block of data for Stub upload.
 * Verifies checksum before acknowledging.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet
 */
static void HandleMemData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t seq = ReadLE32(pkt->data);

    EsptoolHal_LogD(TAG, "MEM_DATA seq=%lu len=%u", seq, pkt->size);
    EsptoolHal_LogI("ESP", "  seq=%lu len=%u", seq, pkt->size);

    /* Verify checksum: payload starts at offset 16 */
    if (pkt->size > 16) {
        const uint8_t *payload = &pkt->data[16];
        int payload_len = pkt->size - 16;
        uint8_t expected = Esptool_CalcChecksum(payload, payload_len);
        uint8_t received = (uint8_t)(pkt->value & 0xFF);
        if (expected != received) {
            EsptoolHal_LogD(TAG,
                        "MEM_DATA checksum mismatch: "
                        "expected=0x%02X received=0x%02X",
                        expected, received);
            EsptoolHal_LogI("ESP",
                "  Checksum mismatch: expected=0x%02X received=0x%02X",
                expected, received);
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_MEM_DATA, ctx->last_read_val,
                                   ESP_FAIL, status_len, NULL, status_len);
            return;
        }
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_MEM_DATA, ctx->last_read_val, ESP_OK,
                           status_len, NULL, status_len);
}

/*
 * HandleMemEnd - Handle MEM_END command (0x06)
 *
 * Completes memory write session. Sends "OHAI" handshake
 * to indicate Stub is ready. Transitions state to READY.
 *
 * @ctx: Pointer to protocol context
 * @pkt: Pointer to parsed request packet (data = [execute:4][entry_point:4])
 */
static void HandleMemEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t execute = ReadLE32(pkt->data);

    EsptoolHal_LogD(TAG, "MEM_END execute=%lu", execute);
    EsptoolHal_LogI("ESP", "  execute=%lu", execute);
    EsptoolHal_LogD(TAG, "MEM_END: stub_mode before=%d", ctx->stub_mode);

    ctx->state = ESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_MEM_END, ctx->last_read_val, ESP_OK,
                           status_len, NULL, status_len);

    /* Send "OHAI" handshake after MEM_END to indicate stub is ready.
       Real device sends OHAI regardless of execute flag. */
    if (!ctx->stub_mode) {
        uint8_t ohai[] = {0xC0, 'O', 'H', 'A', 'I', 0xC0};
        EsptoolHal_Write(ohai, sizeof(ohai));
        ctx->stub_mode = true;
        EsptoolHal_LogI("ESP", "  Stub mode: OHAI sent");
        EsptoolHal_LogD(TAG, "MEM_END: OHAI sent, stub_mode=true");
    } else {
        EsptoolHal_LogD(TAG, "MEM_END: stub_mode already true, skipping OHAI");
    }
}

/*
 * HandleFlashDeflBegin - Handle compressed flash write begin command
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
static void HandleFlashDeflBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    /* FLASH_DEFL_BEGIN format:
       [uncompressed_size:4][num_blocks:4][block_size:4][offset:4] */
    uint32_t uncompressed_size = ReadLE32(pkt->data);
    uint32_t offset = ReadLE32(pkt->data + 12);

    /* ROM mode sends extra 4 bytes for encrypted flag */
    uint32_t encrypted = 0;
    if (pkt->size >= 20) {
        encrypted = ReadLE32(pkt->data + 16);
    }

    /* Release mode: reject plaintext writes when encryption is active */
    if (!encrypted && Efuse_IsFlashEncryptionEnabled(ctx->chip) &&
        Efuse_IsDownloadEncryptDisabled(ctx->chip)) {
        EsptoolHal_LogD(
            TAG,
            "FLASH_DEFL_BEGIN rejected: release mode, plaintext not allowed");
        EsptoolHal_LogE("ERR",
                       "  Release mode: plaintext flash disabled");
        uint8_t status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN,
                               ctx->last_read_val, ESP_FAIL, status_len, NULL,
                               status_len);
        return;
    }

    EsptoolHal_LogD(TAG,
                "FLASH_DEFL_BEGIN: flash_encrypted=%d "
                "chip_type=%d stub_mode=%d",
                encrypted, ctx->chip->type, ctx->stub_mode);
    EsptoolHal_LogD(TAG,
                "FLASH_DEFL_BEGIN: IsFlashEncryptionEnabled=%d "
                "IsDownloadEncryptDisabled=%d",
                Efuse_IsFlashEncryptionEnabled(ctx->chip),
                Efuse_IsDownloadEncryptDisabled(ctx->chip));

    /* Log key availability */
#ifdef ENABLE_TRACE_PROTO
    {
        int key_len = 0;
        int key_offset = Efuse_GetEncryptionKeyOffset(ctx->chip, &key_len);
        EsptoolHal_LogD(TAG,
                    "FLASH_DEFL_BEGIN: key_offset=0x%02X key_len=%d efuse=%p "
                    "efuse_size=%d",
                    key_offset, key_len, ctx->chip->efuse,
                    ctx->chip->efuse_size);
        if (key_offset >= 0 && ctx->chip->efuse &&
            key_offset + key_len <= ctx->chip->efuse_size) {
            const uint8_t *key = &ctx->chip->efuse[key_offset];
            EsptoolHal_LogD(TAG,
                        "FLASH_DEFL_BEGIN: Key first 8 bytes: %02X %02X %02X "
                        "%02X %02X %02X %02X %02X",
                        key[0], key[1], key[2], key[3], key[4], key[5], key[6],
                        key[7]);
        } else {
            EsptoolHal_LogD(TAG, "FLASH_DEFL_BEGIN: No encryption key available");
        }
    }
#endif

    /* Flush any pending accumulated data from previous session */
    if (ctx->defl_buf && ctx->defl_buf_size > 0) {
        EsptoolHal_LogD(TAG, "FLASH_DEFL_BEGIN: flushing previous accumulation");
        EsptoolHal_LogI("ESP",
                       "  Flushing previous compressed data");
        uint32_t ret = Defl_FlushBuffer(ctx);
        if (ret != ESP_OK) {
            EsptoolHal_LogD(TAG, "FLASH_DEFL_BEGIN flush previous failed");
            EsptoolHal_LogE("ERR",
                           "  Failed to flush previous compressed data");
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN,
                                   ctx->last_read_val, ESP_FAIL, status_len,
                                   NULL, status_len);
            return;
        }
    }
    /* Note: Defl_FreeBuffer is called inside Defl_FlushBuffer on both success
     * and failure */

    /* Save deflate session info */
    ctx->defl_offset = offset;
    ctx->defl_unc_size = uncompressed_size;

    /* Update protocol state */
    ctx->flash_offset = offset;
    ctx->flash_seq = 0;
    ctx->flash_uncompressed_size = uncompressed_size;
    ctx->flash_encrypted = (encrypted != 0);
    ctx->state = ESP_STATE_FLASH_WRITING;

    /* Erase the flash region (use uncompressed_size for erase calculation) */
    if (uncompressed_size > 0) {
        Flash_Erase(ctx->flash, offset, uncompressed_size);
        EsptoolHal_Modified();
        EsptoolHal_LogI("ESP",
                        "  Flash erased: offset=0x%08lX size=%lu", offset,
                        uncompressed_size);
    }

    /* Allocate accumulation buffer */
    if (uncompressed_size > 0) {
        ctx->defl_buf =
            (uint8_t *)EsptoolHal_MemAlloc(uncompressed_size);
        if (!ctx->defl_buf) {
            EsptoolHal_LogD(TAG,
                        "Failed to allocate deflate buffer: "
                        "%lu bytes",
                        uncompressed_size);
            EsptoolHal_LogE("ERR",
                            "  Failed to allocate deflate buffer: %lu bytes",
                            uncompressed_size);
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN,
                                   ctx->last_read_val, ESP_FAIL, status_len,
                                   NULL, status_len);
            return;
        }
        ctx->defl_buf_cap = uncompressed_size;
        ctx->defl_buf_size = 0;
        EsptoolHal_LogD(TAG, "FLASH_DEFL_BEGIN: allocated %lu bytes buffer",
                    uncompressed_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    /* Use request's value field (checksum) for response Val, not last_read_val
     */
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN, pkt->value, ESP_OK,
                           status_len, NULL, status_len);
}

/*
 * HandleFlashDeflData - Handle compressed flash data block
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
static void HandleFlashDeflData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t data_len = ReadLE32(pkt->data);
    uint32_t seq = ReadLE32(pkt->data + 4);

    EsptoolHal_LogD(
        TAG, "HandleFlashDeflData: seq=%lu data_len=%lu pkt->size=%u state=%d",
        seq, data_len, pkt->size, ctx->state);
    EsptoolHal_LogD(TAG, "FLASH_DEFL_DATA seq=%lu len=%lu", seq, data_len);
    EsptoolHal_LogI("ESP", "  seq=%lu len=%lu", seq, data_len);

    /* Verify sequence number */
    if (seq != ctx->flash_seq) {
        EsptoolHal_LogD(TAG,
                    "FLASH_DEFL_DATA seq mismatch: expected=%lu received=%lu",
                    ctx->flash_seq, seq);
        EsptoolHal_LogI("ESP",
                        "  Seq mismatch: expected=%lu received=%lu",
                        ctx->flash_seq, seq);
        uint8_t status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val,
                               ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    if (pkt->size >= 16 && data_len <= (uint32_t)(pkt->size - 16)) {
        const uint8_t *payload = &pkt->data[16];

        /* Verify checksum */
        uint8_t expected = Esptool_CalcChecksum(payload, (int)data_len);
        uint8_t received = (uint8_t)(pkt->value & 0xFF);
        if (expected != received) {
            EsptoolHal_LogD(TAG,
                        "FLASH_DEFL_DATA checksum mismatch: expected=0x%02X "
                        "received=0x%02X",
                        expected, received);
            EsptoolHal_LogI("ESP",
                            "  Checksum mismatch: expected=0x%02X "
                            "received=0x%02X",
                            expected, received);
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA,
                                   ctx->last_read_val, ESP_FAIL, status_len,
                                   NULL, status_len);
            return;
        }

        /* Accumulate compressed data into buffer */
        if (ctx->defl_buf_cap > 0 && data_len > 0) {
            /* Check buffer overflow */
            if (ctx->defl_buf_size + data_len > ctx->defl_buf_cap) {
                EsptoolHal_LogD(TAG,
                            "Deflate buffer overflow: size=%lu cap=%lu add=%lu",
                            ctx->defl_buf_size, ctx->defl_buf_cap, data_len);
                EsptoolHal_LogE("ERR",
                                "  Deflate buffer overflow");
                Defl_FreeBuffer(ctx);
                uint8_t status_len = ESP_STATUS_LEN(ctx);
                Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA,
                                       ctx->last_read_val, ESP_FAIL, status_len,
                                       NULL, status_len);
                return;
            }

            memcpy(ctx->defl_buf + ctx->defl_buf_size, payload, data_len);
            ctx->defl_buf_size += data_len;

            EsptoolHal_LogD(TAG, "FLASH_DEFL_DATA accumulated %lu/%lu bytes",
                        ctx->defl_buf_size, ctx->defl_buf_cap);
            EsptoolHal_LogI("ESP",
                            "  Accumulated %lu/%lu bytes", ctx->defl_buf_size,
                            ctx->defl_buf_cap);
        }

        ctx->flash_seq = seq + 1;
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val,
                           ESP_OK, status_len, NULL, status_len);
}

/*
 * HandleFlashDeflEnd - Handle compressed flash write end command
 *
 * FLASH_DEFL_END (0x12) completes a compressed flash write session.
 * Decompresses all accumulated data and writes it to flash.
 *
 * Request format: [reboot:4] (0=don't reboot, 1=reboot)
 */
static void HandleFlashDeflEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    EsptoolHal_LogD(TAG, "FLASH_DEFL_END");
    EsptoolHal_LogI("ESP", "  End compressed flash download");

    /* Decompress accumulated data and write to flash */
    uint32_t ret = Defl_FlushBuffer(ctx);
    if (ret != ESP_OK) {
        EsptoolHal_LogD(TAG, "FLASH_DEFL_END flush failed");
        EsptoolHal_LogE("ERR", "  Decompression flush failed");
        ctx->state = ESP_STATE_READY;
        uint8_t status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_END, ctx->last_read_val,
                               ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    ctx->state = ESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_END, ctx->last_read_val,
                           ESP_OK, status_len, NULL, status_len);
}

/*
 * HandleReadFlash - Handle flash read command (stub-only)
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
static void HandleReadFlash(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t addr = ReadLE32(pkt->data);
    uint32_t len = ReadLE32(pkt->data + 4);
    uint32_t bsize = ReadLE32(pkt->data + 8);
    uint32_t psize = ReadLE32(pkt->data + 12);

    (void)psize;

    EsptoolHal_LogD(TAG, "READ_FLASH addr=0x%08lX len=%lu bsize=%lu psize=%lu",
                addr, len, bsize, psize);
    EsptoolHal_LogI("ESP", "  addr=0x%08lX len=%lu bsize=%lu",
                    addr, len, bsize);

    /* Step 1: Send command ACK (2-byte status in command response) */
    Esptool_SendResponseEx(ctx, ESP_CMD_READ_FLASH, ctx->last_read_val, ESP_OK,
                           2, NULL, 2);

    /* Allocate buffers once before the loop */
    uint8_t *buf = (uint8_t *)EsptoolHal_MemAlloc(bsize);
    if (!buf) {
        EsptoolHal_LogE("ERR",
                       "  Failed to allocate read buffer");
        return;
    }

    uint32_t encoded_max = bsize * 2 + 2;
    uint8_t *encoded = (uint8_t *)EsptoolHal_MemAlloc(encoded_max);
    if (!encoded) {
        EsptoolHal_MemFree(buf);
        EsptoolHal_LogE("ERR",
                       "  Failed to allocate encode buffer");
        return;
    }

    /* Step 2: Send flash data as separate SLIP frames */
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t chunk_size = len - offset;
        if (chunk_size > bsize) {
            chunk_size = bsize;
        }

        if (!Flash_Read(ctx->flash, addr + offset, buf, chunk_size)) {
            EsptoolHal_LogE("ERR",
                            "  Flash read failed at offset 0x%08lX",
                            addr + offset);
            break;
        }

        /* Decrypt if flash encryption is enabled */
        Esptool_DecryptInPlace(ctx, buf, chunk_size, addr + offset);

        /* SLIP-encode the chunk and send as raw frame */
        int enc_len =
            Slip_Encode(buf, (int)chunk_size, encoded, (int)encoded_max);
        if (enc_len > 0) {
            EsptoolHal_Write(encoded, (uint32_t)enc_len);
        }

        offset += chunk_size;
    }

    /* Free buffers after the loop */
    EsptoolHal_MemFree(encoded);
    EsptoolHal_MemFree(buf);

    /* Step 3: Calculate and send 16-byte MD5 digest as final SLIP frame */
    uint8_t md5[16];
    Flash_CalcMd5(ctx->flash, addr, len, md5);

    uint8_t md5_encoded[34]; /* 16 bytes * 2 (worst case escaping) + 2 (framing) */
    int md5_enc_len = Slip_Encode(md5, 16, md5_encoded, sizeof(md5_encoded));
    if (md5_enc_len > 0) {
        EsptoolHal_Write(md5_encoded, (uint32_t)md5_enc_len);
    }

    EsptoolHal_LogD(TAG, "READ_FLASH complete: %lu bytes sent", len);
    EsptoolHal_LogI("ESP", "  Read complete: %lu bytes", len);
}

/*
 * HandleEraseFlash - Handle erase entire flash command (stub-only)
 *
 * ERASE_FLASH (0xD0) erases the entire flash memory.
 * Also frees any pending deflate buffer.
 */
static void HandleEraseFlash(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    EsptoolHal_LogD(TAG, "ERASE_FLASH");
    EsptoolHal_LogI("ESP", "  Erase entire flash");

    /* Free any pending deflate buffer */
    Defl_FreeBuffer(ctx);

    Flash_EraseAll(ctx->flash);
    EsptoolHal_Modified();
    /* ERASE_FLASH is stub-only, always returns 2-byte status */
    Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_FLASH, ctx->last_read_val, ESP_OK,
                           2, NULL, 2);
}

/*
 * HandleEraseBlock - Handle erase flash region command (stub-only)
 *
 * ERASE_REGION (0xD1) erases a specified flash region.
 * Request format: [offset:4][erase_len:4]
 *
 * Flash erase is sector-aligned (4KB boundaries).
 */
static void HandleEraseBlock(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 8);
    uint32_t offset = ReadLE32(pkt->data);
    uint32_t len = ReadLE32(pkt->data + 4);

    EsptoolHal_LogD(TAG, "ERASE_BLOCK offset=0x%08lX len=%lu", offset, len);
    EsptoolHal_LogI("ESP", "  offset=0x%08lX len=%lu", offset,
                    len);

    /* Free any pending deflate buffer */
    Defl_FreeBuffer(ctx);

    /* ERASE_REGION is stub-only, always returns 2-byte status */
    if (Flash_Erase(ctx->flash, offset, len)) {
        EsptoolHal_Modified();
        Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_REGION, ctx->last_read_val,
                               ESP_OK, 2, NULL, 2);
    } else {
        Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_REGION, ctx->last_read_val,
                               ESP_FAIL, 2, NULL, 2);
    }
}

/*
 * HandleFlashMd5 - Handle flash MD5 calculation command
 *
 * SPI_FLASH_MD5 (0x13) calculates MD5 hash of a flash region.
 *
 * Request format: [addr:4][len:4][padding:8]
 *
 * Response format differs by mode:
 * - ROM mode:   32-byte ASCII hex MD5 + 2-byte status
 * - Stub mode:  16-byte binary MD5 + 2-byte status
 */
static void HandleFlashMd5(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 8);
    uint32_t addr = ReadLE32(pkt->data);
    uint32_t len = ReadLE32(pkt->data + 4);

    EsptoolHal_LogD(TAG, "FLASH_MD5 addr=0x%08lX len=%lu", addr, len);
    EsptoolHal_LogI("ESP", "  addr=0x%08lX len=%lu", addr, len);

    uint8_t md5[16];
    Flash_CalcMd5(ctx->flash, addr, len, md5);

    /* Response format: [data:N][status:2]
       ROM mode:   [md5_hex:32][status:2] = 34 bytes
       Stub mode:  [md5_bin:16][status:2] = 18 bytes */
    if (ctx->stub_mode) {
        /* Stub mode: return 16-byte binary MD5 + 2-byte status */
        uint8_t resp[18];
        memcpy(&resp[0], md5, 16);
        resp[16] = 0x00; /* status byte 1 (success) */
        resp[17] = 0x00; /* status byte 2 (success) */
        EsptoolHal_LogD(TAG, "  MD5 (stub, binary)");
        EsptoolHal_LogI("ESP", "  MD5 (stub, binary)");
        Esptool_SendResponseEx(ctx, ESP_CMD_SPI_FLASH_MD5, ctx->last_read_val,
                               ESP_OK, 2, resp, 18);
    } else {
        /* ROM mode: return 32-byte ASCII hex MD5 + 2-byte status */
        uint8_t resp[34];
        for (int i = 0; i < 16; i++) {
            snprintf((char *)&resp[i * 2], 3, "%02x", md5[i]);
        }
        resp[32] = 0x00; /* status byte 1 (success) */
        resp[33] = 0x00; /* status byte 2 (success) */
        EsptoolHal_LogD(TAG, "  MD5=%.*s", 32, resp);
        EsptoolHal_LogI("ESP", "  MD5=%.*hs", 32, resp);
        Esptool_SendResponseEx(ctx, ESP_CMD_SPI_FLASH_MD5, ctx->last_read_val,
                               ESP_OK, 2, resp, 34);
    }
}

/*
 * HandleFlashBegin - Handle flash write begin command
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
static void HandleFlashBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t erase_size = ReadLE32(pkt->data);
    uint32_t num_blocks = ReadLE32(pkt->data + 4);
    uint32_t block_size = ReadLE32(pkt->data + 8);
    uint32_t offset = ReadLE32(pkt->data + 12);

    /* ROM mode sends extra 4 bytes for encrypted flag */
    uint32_t encrypted = 0;
    if (pkt->size >= 20) {
        encrypted = ReadLE32(pkt->data + 16);
    }

    /* Release mode: reject plaintext writes when encryption is active */
    if (!encrypted && Efuse_IsFlashEncryptionEnabled(ctx->chip) &&
        Efuse_IsDownloadEncryptDisabled(ctx->chip)) {
        EsptoolHal_LogD(
            TAG, "FLASH_BEGIN rejected: release mode, plaintext not allowed");
        EsptoolHal_LogE("ERR",
                       "  Release mode: plaintext flash disabled");
        uint8_t status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_BEGIN, ctx->last_read_val,
                               ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    /* Note: Do NOT free deflate buffer here.
       Client may send FLASH_BEGIN before FLASH_DEFL_END.
       The buffer will be flushed by HandleFlashDeflEnd. */

    ctx->flash_offset = offset;
    ctx->flash_seq = 0;
    ctx->flash_encrypted = (encrypted != 0);
    ctx->state = ESP_STATE_FLASH_WRITING;

    EsptoolHal_LogD(TAG,
                "FLASH_BEGIN erase=%lu blocks=%lu bsize=%lu offset=0x%08lX "
                "encrypted=%lu",
                erase_size, num_blocks, block_size, offset, encrypted);
    EsptoolHal_LogI("ESP",
        "  erase=%lu blocks=%lu bsize=%lu offset=0x%08lX encrypted=%lu",
        erase_size, num_blocks, block_size, offset, encrypted);

    /* Erase the flash region as requested by the host */
    if (erase_size > 0) {
        Flash_Erase(ctx->flash, offset, erase_size);
        EsptoolHal_Modified();
        EsptoolHal_LogI("ESP",
                        "  Flash erased: offset=0x%08lX size=%lu", offset,
                        erase_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_BEGIN, pkt->value, ESP_OK,
                           status_len, NULL, status_len);
}

/*
 * HandleFlashData - Handle flash write data block
 *
 * FLASH_DATA (0x03) receives a block of uncompressed data to write to flash.
 *
 * Request format: [data_len:4][seq:4][padding:4][padding:4][data:data_len]
 *
 * Verifies sequence number and checksum before writing to flash.
 * Flash write uses AND operation to simulate real Flash behavior
 * (bits can only be cleared, not set).
 */
static void HandleFlashData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 16);
    uint32_t data_len = ReadLE32(pkt->data);
    uint32_t seq = ReadLE32(pkt->data + 4);

    EsptoolHal_LogD(TAG, "FLASH_DATA seq=%lu len=%lu", seq, data_len);
    EsptoolHal_LogI("ESP", "  seq=%lu len=%lu", seq, data_len);

    /* Verify sequence number */
    if (seq != ctx->flash_seq) {
        EsptoolHal_LogD(TAG, "FLASH_DATA seq mismatch: expected=%lu received=%lu",
                    ctx->flash_seq, seq);
        EsptoolHal_LogI("ESP",
                        "  Seq mismatch: expected=%lu received=%lu",
                        ctx->flash_seq, seq);
        uint8_t status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val,
                               ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    if (pkt->size >= 16 && data_len <= (uint32_t)(pkt->size - 16)) {
        uint8_t *payload = (uint8_t *)&pkt->data[16];

        /* Verify checksum */
        uint8_t expected = Esptool_CalcChecksum(payload, (int)data_len);
        uint8_t received = (uint8_t)(pkt->value & 0xFF);
        if (expected != received) {
            EsptoolHal_LogD(
                TAG,
                "FLASH_DATA checksum mismatch: expected=0x%02X received=0x%02X",
                expected, received);
            EsptoolHal_LogI("ESP",
                            "  Checksum mismatch: expected=0x%02X "
                            "received=0x%02X",
                            expected, received);
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val,
                                   ESP_FAIL, status_len, NULL, status_len);
            return;
        }

        /* Encrypt if encrypted flag was set */
        if (Esptool_EncryptInPlace(ctx, payload, data_len, ctx->flash_offset) !=
            ESP_OK) {
            EsptoolHal_LogE("ERR",
                            "  Encryption failed at offset 0x%08lX",
                            ctx->flash_offset);
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val,
                                   ESP_FAIL, status_len, NULL, status_len);
            return;
        }
        Flash_Write(ctx->flash, ctx->flash_offset, payload, data_len);

        ctx->flash_offset += data_len;
        ctx->flash_seq = seq + 1;
    }

    EsptoolHal_Modified();
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val, ESP_OK,
                           status_len, NULL, status_len);
}

/*
 * HandleFlashEnd - Handle flash write end command
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
static void HandleFlashEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    CHECK_PKT_SIZE(pkt, 4);
    uint32_t reboot = ReadLE32(pkt->data);

    EsptoolHal_LogD(TAG, "FLASH_END reboot=%lu", reboot);
    EsptoolHal_LogI("ESP", "  reboot=%lu", reboot);

    /* Flush any pending deflate buffer (ROM mode scenario) */
    if (ctx->defl_buf && ctx->defl_buf_size > 0) {
        EsptoolHal_LogD(TAG,
                    "FLASH_END: flushing pending deflate buffer (%lu bytes)",
                    ctx->defl_buf_size);
        EsptoolHal_LogI("ESP",
                       "  Flushing pending compressed data");
        if (Defl_FlushBuffer(ctx) != ESP_OK) {
            EsptoolHal_LogD(TAG, "FLASH_END: flush failed");
            EsptoolHal_LogE("ERR",
                           "  Failed to flush compressed data");
        }
    }

    ctx->state = ESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_END, pkt->value, ESP_OK,
                           status_len, NULL, status_len);
}

/*
 * HandleGetSecurityInfo - Handle get security info command
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
static void HandleGetSecurityInfo(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    EsptoolHal_LogD(TAG, "GET_SECURITY_INFO");
    EsptoolHal_LogI("ESP", "  Get security info");

    /* ESP8266/ESP32 ROM and stub do not support GET_SECURITY_INFO.
       Return normal response with failure status (FF 00), matching real device
       behavior. esptool sees status != 0 and falls back to magic value
       detection. */
    if (ctx->chip->type == CHIP_ESP8266 || ctx->chip->type == CHIP_ESP32) {
        EsptoolHal_LogD(TAG, "  Not supported on %s, returning failure",
                    ctx->chip->name);
        EsptoolHal_LogI("ESP", "  Not supported on %hs",
                        ctx->chip->name);
        uint8_t err[2] = {0xFF, 0x00};
        Esptool_SendResponseEx(ctx, ESP_CMD_GET_SECURITY_INFO,
                               ctx->last_read_val, ESP_FAIL, 2, err, 2);
        return;
    }

    /* ESP32-S2: Return 14-byte response (12 payload + 2 status, no chip_id).
       esptool tries resp_data_len=20 first (fails), then resp_data_len=12
       (succeeds). chip_id will be None, causing get_chip_id() to raise
       FatalError, which triggers fallback to magic value detection. */
    if (ctx->chip->type == CHIP_ESP32S2) {
        EsptoolHal_LogD(TAG, "  ESP32-S2: returning 14-byte response (no chip_id)");
        EsptoolHal_LogI("ESP",
                       "  ESP32-S2: no chip_id in response");
        uint32_t flash_crypt_cnt = Efuse_GetFlashCryptCnt(ctx->chip);
        EsptoolHal_LogI("ESP",
                        "  flags=0x%08lX flash_crypt_cnt=%u", 0UL,
                        (unsigned)flash_crypt_cnt);
        uint8_t sec_data[14] = {0};
        /* bytes 0-3:   flags (all zeros) */
        /* byte 4:      flash_crypt_cnt */
        sec_data[4] = (uint8_t)(flash_crypt_cnt & 0xFF);
        /* bytes 5-11:  key_purposes (7 bytes, one per key block KEY0-KEY5 +
         * reserved) */
        for (int i = 0; i < 7; i++) {
            sec_data[5 + i] = Efuse_GetKeyPurpose(ctx->chip, i);
        }
        /* bytes 12-13: status = success (0x00, 0x00) */
        Esptool_SendResponse(ctx, ESP_CMD_GET_SECURITY_INFO, ctx->last_read_val,
                             ESP_OK, sec_data, 14);
        return;
    }

    /* ESP32 stub, ESP32-S3, C2, C3, C6: Return 22-byte response with
       IMAGE_CHIP_ID.
       [flags:4][flash_crypt_cnt:1][key_purposes:7][chip_id:4][api_version:4][status:2]
       For ESP32 stub, chip_id = EFUSE_CHIP_ID (0x00F01D83). */
    uint32_t chip_id = (ctx->chip->type == CHIP_ESP32)
                        ? ctx->chip->chip_id
                        : ctx->chip->security_chip_id;
    uint32_t flash_crypt_cnt = Efuse_GetFlashCryptCnt(ctx->chip);

    uint8_t sec_data[22] = {0};
    /* bytes 0-3:   flags (all zeros) */
    /* byte 4:      flash_crypt_cnt */
    sec_data[4] = (uint8_t)(flash_crypt_cnt & 0xFF);
    /* bytes 5-11:  key_purposes (7 bytes, one per key block KEY0-KEY5 +
     * reserved) */
    for (int i = 0; i < 7; i++) {
        sec_data[5 + i] = Efuse_GetKeyPurpose(ctx->chip, i);
    }
    /* bytes 12-15: chip_id (IMAGE_CHIP_ID, little-endian) */
    sec_data[12] = (uint8_t)(chip_id & 0xFF);
    sec_data[13] = (uint8_t)((chip_id >> 8) & 0xFF);
    sec_data[14] = (uint8_t)((chip_id >> 16) & 0xFF);
    sec_data[15] = (uint8_t)((chip_id >> 24) & 0xFF);
    /* bytes 16-19: api_version (0) */
    /* bytes 20-21: status = success (0x00, 0x00) */

    EsptoolHal_LogD(TAG, "  chip_id (IMAGE_CHIP_ID)=%lu (0x%08lX)", chip_id,
                chip_id);
    EsptoolHal_LogI("ESP", "  flags=0x%08lX flash_crypt_cnt=%u",
                    0UL, (unsigned)flash_crypt_cnt);
    EsptoolHal_LogI("ESP",
                    "  chip_id=%lu (0x%08lX) api_version=%lu", chip_id,
                    chip_id, 0UL);

    /* Transition to READY state when chip detection succeeds via
     * GET_SECURITY_INFO */
    if (ctx->state == ESP_STATE_SYNCED) {
        ctx->state = ESP_STATE_READY;
        EsptoolHal_LogI(
            "ESP",
            "  Chip detected via security info, ready for commands");
    }

    Esptool_SendResponse(ctx, ESP_CMD_GET_SECURITY_INFO, ctx->last_read_val,
                         ESP_OK, sec_data, 22);
}

/*
 * HandleSpiAttach - Handle SPI flash attach command
 *
 * SPI_ATTACH (0x0D) initializes the SPI flash controller.
 * In the simulator, this is a no-op that always succeeds.
 */
static void HandleSpiAttach(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    EsptoolHal_LogD(TAG, "SPI_ATTACH");
    EsptoolHal_LogI("ESP", "  Attach SPI flash");

    /* SPI_ATTACH: ROM mode 4-byte status, stub mode 2-byte status */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_SPI_ATTACH, ctx->last_read_val, ESP_OK,
                           status_len, NULL, status_len);
}

/*
 * HandleSpiSetParams - Handle SPI flash parameters command
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
static void HandleSpiSetParams(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    uint32_t fl_id = 0, total_size = 0, block_size = 0, sector_size = 0,
          page_size = 0, status_mask = 0;

    if (pkt->size >= 24) {
        fl_id = ReadLE32(pkt->data);
        total_size = ReadLE32(pkt->data + 4);
        block_size = ReadLE32(pkt->data + 8);
        sector_size = ReadLE32(pkt->data + 12);
        page_size = ReadLE32(pkt->data + 16);
        status_mask = ReadLE32(pkt->data + 20);
    }

    EsptoolHal_LogD(TAG,
                "SPI_SET_PARAMS fl_id=0x%08lX total=%lu block=%lu sector=%lu "
                "page=%lu mask=0x%08lX",
                fl_id, total_size, block_size, sector_size, page_size,
                status_mask);
    EsptoolHal_LogI("ESP",
        "  fl_id=0x%08lX total=%lu block=%lu sector=%lu page=%lu mask=0x%08lX",
        fl_id, total_size, block_size, sector_size, page_size, status_mask);

    /* ESP8266 ROM does not support SPI_SET_PARAMS.
       Return ROM_INVALID_RECV_MSG error so esptool falls back gracefully. */
    if (ctx->chip->type == CHIP_ESP8266 && !ctx->stub_mode) {
        EsptoolHal_LogD(TAG, "  Not supported on ESP8266 ROM, returning error");
        EsptoolHal_LogI("ESP", "  Not supported on ESP8266 ROM");
        uint8_t err_data[4] = {0x01, 0x05, 0x00, 0x00};
        Esptool_SendResponse(ctx, ESP_CMD_SPI_SET_PARAMS, ctx->last_read_val,
                             ESP_OK, err_data, 4);
        return;
    }

    /* ESP32+ ROM and all stubs: return success.
       ROM mode: 4-byte status; stub mode: 2-byte status. */
    uint8_t status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_SPI_SET_PARAMS, ctx->last_read_val,
                           ESP_OK, status_len, NULL, status_len);
}

/*
 * HandleRunUserCode - Handle soft reset command (stub-only)
 *
 * RUN_USER_CODE (0xD3) triggers a soft reset by jumping to user code.
 * This is a fire-and-forget command - client does not wait for response.
 * After sending response, resets protocol state for next connection.
 */
static void HandleRunUserCode(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    EsptoolHal_LogD(TAG, "RUN_USER_CODE");
    EsptoolHal_LogI("ESP", "  Run user code (soft reset)");

    /* RUN_USER_CODE: stub-only, fire-and-forget (client does not wait for
     * response) */
    Esptool_SendResponseEx(ctx, ESP_CMD_RUN_USER_CODE, ctx->last_read_val,
                           ESP_OK, 2, NULL, 2);

    /* Reset protocol state for next connection */
    Esptool_ResetState(ctx);
}

/*
 * Esptool_ProcessFrame - Process a complete SLIP frame
 *
 * Parses the frame into an ESP_PACKET, validates direction (must be request),
 * checks protocol state, and dispatches to the appropriate command handler.
 *
 * @ctx:       Pointer to esptool context
 * @frame:     Pointer to decoded SLIP frame data
 * @frame_len: Length of frame data in bytes
 *
 * Returns true if frame was processed successfully, false on error.
 */
bool Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const uint8_t *frame, int frame_len)
{
    ESP_PACKET *pkt = &ctx->pkt;

    EsptoolHal_LogD(TAG, "RX frame len=%d", frame_len);

    if (!ParsePacket(frame, frame_len, pkt)) {
        EsptoolHal_LogD(TAG, "Invalid packet");
        return false;
    }

    /* Get command name */
    const char *cmdName = GetCmdName(pkt->command);

    /* Get direction string */
    const char *dirStr = (pkt->direction == ESP_DIR_REQUEST) ? "REQ" : "RES";

    /* Log packet summary */
    EsptoolHal_LogI("ESP", "[%s] %hs size=%u val=0x%08lX",
                    dirStr, cmdName, pkt->size, pkt->value);

    if (pkt->direction != ESP_DIR_REQUEST) {
        EsptoolHal_LogD(TAG, "Not a request: 0x%02X", pkt->direction);
        return false;
    }

    /* Download mode disabled: ignore all commands (simulate ROM not entering
     * download mode) */
    if (Efuse_IsDownloadModeDisabled(ctx->chip)) {
        EsptoolHal_LogD(TAG, "Download mode disabled, ignoring command 0x%02X",
                    pkt->command);
        EsptoolHal_LogI("ESP",
                       "  Download mode disabled, command ignored");
        return false;
    }

    /* Secure download mode: only allow flash-related commands */
    if (Efuse_IsSecureDownloadEnabled(ctx->chip)) {
        bool allowed = false;
        switch (pkt->command) {
        case ESP_CMD_SYNC:
        case ESP_CMD_READ_REG:
        case ESP_CMD_WRITE_REG:
        case ESP_CMD_SPI_ATTACH:
        case ESP_CMD_CHANGE_BAUDRATE:
        case ESP_CMD_GET_SECURITY_INFO:
        case ESP_CMD_FLASH_BEGIN:
        case ESP_CMD_FLASH_DATA:
        case ESP_CMD_FLASH_END:
        case ESP_CMD_FLASH_DEFL_BEGIN:
        case ESP_CMD_FLASH_DEFL_DATA:
        case ESP_CMD_FLASH_DEFL_END:
        case ESP_CMD_SPI_SET_PARAMS:
        case ESP_CMD_SPI_FLASH_MD5:
            allowed = true;
            break;
        default:
            allowed = false;
            break;
        }
        if (!allowed) {
            EsptoolHal_LogD(TAG, "Secure download: command 0x%02X not allowed",
                        pkt->command);
            EsptoolHal_LogI("ESP",
                            "  Secure download: command 0x%02X rejected",
                            pkt->command);
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, pkt->command, pkt->value, ESP_FAIL,
                                   status_len, NULL, status_len);
            return false;
        }
    }

    /* State validation: check if command is allowed in current state */
    bool valid = true;
    switch (pkt->command) {
    /* SYNC is always allowed (resets to SYNCED) */
    case ESP_CMD_SYNC:
        break;

    /* Commands allowed in IDLE state (before SYNC) - none except SYNC */
    /* Commands allowed in SYNCED state (after SYNC, before chip detection) */
    case ESP_CMD_READ_REG:
    case ESP_CMD_WRITE_REG:
    case ESP_CMD_SPI_ATTACH:
    case ESP_CMD_CHANGE_BAUDRATE:
    case ESP_CMD_GET_SECURITY_INFO:
        if (ctx->state < ESP_STATE_SYNCED) {
            valid = false;
        }
        break;

    /* Commands allowed in READY state (after chip detection) */
    case ESP_CMD_FLASH_BEGIN:
    case ESP_CMD_FLASH_DEFL_BEGIN:
    case ESP_CMD_MEM_BEGIN:
    case ESP_CMD_SPI_FLASH_MD5:
    case ESP_CMD_SPI_SET_PARAMS:
    case ESP_CMD_ERASE_FLASH:
    case ESP_CMD_ERASE_REGION:
    case ESP_CMD_READ_FLASH:
    case ESP_CMD_RUN_USER_CODE:
        if (ctx->state < ESP_STATE_READY) {
            valid = false;
        }
        break;

    /* Flash data commands require FLASH_WRITING state */
    case ESP_CMD_FLASH_DATA:
    case ESP_CMD_FLASH_END:
    case ESP_CMD_FLASH_DEFL_DATA:
    case ESP_CMD_FLASH_DEFL_END:
        if (ctx->state != ESP_STATE_FLASH_WRITING) {
            valid = false;
        }
        break;

    /* Memory data commands require MEM_WRITING state */
    case ESP_CMD_MEM_DATA:
    case ESP_CMD_MEM_END:
        if (ctx->state != ESP_STATE_MEM_WRITING) {
            valid = false;
        }
        break;

    /* NAND flash commands (0xD5-0xDE) - not implemented in FakeEsptool.
       These commands are stub-only and used by ESP32-S3 etc. for NAND flash.
       Listed here explicitly for documentation; they will return
       ROM_INVALID_RECV_MSG. */
    case ESP_CMD_SPI_NAND_ATTACH:
    case ESP_CMD_SPI_NAND_READ_SPARE:
    case ESP_CMD_SPI_NAND_WRITE_SPARE:
    case ESP_CMD_SPI_NAND_READ_FLASH:
    case ESP_CMD_SPI_NAND_WRITE_FLASH_BEGIN:
    case ESP_CMD_SPI_NAND_WRITE_FLASH_DATA:
    case ESP_CMD_SPI_NAND_ERASE_FLASH:
    case ESP_CMD_SPI_NAND_ERASE_REGION:
    case ESP_CMD_SPI_NAND_READ_PAGE_DEBUG:
    case ESP_CMD_SPI_NAND_WRITE_FLASH_END:
        /* Fall through to default - not implemented */

    default:
        valid = false;
        break;
    }

    if (!valid) {
        EsptoolHal_LogD(TAG, "Command 0x%02X not allowed in state %d", pkt->command,
                    ctx->state);
        EsptoolHal_LogI("ESP",
                        "  Command 0x%02X rejected (state=%d)", pkt->command,
                        ctx->state);
        uint8_t status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, pkt->command, pkt->value, ESP_FAIL,
                               status_len, NULL, status_len);
        return false;
    }

    switch (pkt->command) {
    case ESP_CMD_SYNC:
        HandleSync(ctx, pkt);
        break;
    case ESP_CMD_READ_REG:
        HandleReadReg(ctx, pkt);
        break;
    case ESP_CMD_WRITE_REG:
        HandleWriteReg(ctx, pkt);
        break;
    case ESP_CMD_SPI_SET_PARAMS:
        HandleSpiSetParams(ctx, pkt);
        break;
    case ESP_CMD_SPI_ATTACH:
        HandleSpiAttach(ctx, pkt);
        break;
    case ESP_CMD_CHANGE_BAUDRATE:
        HandleChangeBaudrate(ctx, pkt);
        break;
    case ESP_CMD_FLASH_BEGIN:
        HandleFlashBegin(ctx, pkt);
        break;
    case ESP_CMD_FLASH_DATA:
        HandleFlashData(ctx, pkt);
        break;
    case ESP_CMD_FLASH_END:
        HandleFlashEnd(ctx, pkt);
        break;
    case ESP_CMD_MEM_BEGIN:
        HandleMemBegin(ctx, pkt);
        break;
    case ESP_CMD_MEM_DATA:
        HandleMemData(ctx, pkt);
        break;
    case ESP_CMD_MEM_END:
        HandleMemEnd(ctx, pkt);
        break;
    case ESP_CMD_FLASH_DEFL_BEGIN:
        HandleFlashDeflBegin(ctx, pkt);
        break;
    case ESP_CMD_FLASH_DEFL_DATA:
        HandleFlashDeflData(ctx, pkt);
        break;
    case ESP_CMD_FLASH_DEFL_END:
        HandleFlashDeflEnd(ctx, pkt);
        break;
    case ESP_CMD_SPI_FLASH_MD5:
        HandleFlashMd5(ctx, pkt);
        break;
    case ESP_CMD_ERASE_FLASH:
        HandleEraseFlash(ctx, pkt);
        break;
    case ESP_CMD_ERASE_REGION:
        HandleEraseBlock(ctx, pkt);
        break;
    case ESP_CMD_READ_FLASH:
        HandleReadFlash(ctx, pkt);
        break;
    case ESP_CMD_GET_SECURITY_INFO:
        HandleGetSecurityInfo(ctx, pkt);
        break;
    case ESP_CMD_RUN_USER_CODE:
        HandleRunUserCode(ctx, pkt);
        break;
    default:
        /* ROM returns ROM_INVALID_RECV_MSG (0x05) for unsupported commands */
        EsptoolHal_LogD(TAG, "Unknown cmd: 0x%02X", pkt->command);
        EsptoolHal_LogI("ESP", "  Unknown command: 0x%02X",
                        pkt->command);
        {
            /* Response format: [status_byte_1 != 0][ROM_INVALID_RECV_MSG] +
             * padding */
            uint8_t err_data[4] = {0x01, 0x05, 0x00, 0x00};
            uint8_t status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, pkt->command, pkt->value, ESP_OK,
                                   status_len, err_data, 4);
        }
        return false;
    }

    return true;
}

/*
 * Esptool_Feed - Feed raw serial data to protocol decoder
 *
 * Processes incoming bytes through SLIP decoder. When a complete
 * frame is assembled, dispatches to Esptool_ProcessFrame.
 *
 * @ctx: Pointer to protocol context
 * @data: Pointer to raw serial data
 * @len:  Number of bytes in data
 *
 * Returns true if at least one complete frame was processed.
 */
bool Esptool_Feed(ESPTOOL_CTX *ctx, const uint8_t *data, int len)
{
    bool got_frame = false;

    for (int i = 0; i < len; i++) {
        if (Slip_PutByte(&ctx->slip, data[i])) {
            const uint8_t *payload = Slip_GetPayload(&ctx->slip);
            int plen = Slip_GetLength(&ctx->slip);

            if (plen >= 8) {
                Esptool_ProcessFrame(ctx, payload, plen);
                got_frame = true;
            }

            Slip_Reset(&ctx->slip);
        }
    }

    return got_frame;
}
