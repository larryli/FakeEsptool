/*
 * esptool.c - esptool protocol handler implementation
 *
 * Parses SLIP frames, routes commands, and sends responses.
 */

#include "esptool.h"
#include "../serial.h"
#include "../utils/trace.h"
#include "../utils/deflate.h"
#include "../utils/encrypt.h"
#include <string.h>
#include <stdio.h>

#if ENABLE_TRACE
static const char *TAG = "ESP";
#endif

/* Command info structure */
typedef struct {
    const char *name;
    const char *desc;
} ESP_CMD_INFO;

/* Command table for protocol logging */
static const ESP_CMD_INFO commandTable[256] = {
    [0x02] = {"FLASH_BEGIN", "Begin flash download"},
    [0x03] = {"FLASH_DATA", "Flash download data"},
    [0x04] = {"FLASH_END", "End flash download"},
    [0x05] = {"MEM_BEGIN", "Begin memory download"},
    [0x06] = {"MEM_END", "End memory download"},
    [0x07] = {"MEM_DATA", "Memory download data"},
    [0x08] = {"SYNC", "Sync handshake"},
    [0x09] = {"WRITE_REG", "Write register"},
    [0x0A] = {"READ_REG", "Read register"},
    [0x0B] = {"SPI_SET_PARAMS", "Set SPI flash parameters"},
    [0x0D] = {"SPI_ATTACH", "Attach SPI flash"},
    [0x0F] = {"CHANGE_BAUDRATE", "Change baud rate"},
    [0x10] = {"FLASH_DEFL_BEGIN", "Begin compressed flash download"},
    [0x11] = {"FLASH_DEFL_DATA", "Compressed flash download data"},
    [0x12] = {"FLASH_DEFL_END", "End compressed flash download"},
    [0x13] = {"SPI_FLASH_MD5", "Calculate flash MD5"},
    [0x14] = {"GET_SECURITY_INFO", "Get security info"},
    [0xD0] = {"ERASE_FLASH", "Erase entire flash"},
    [0xD1] = {"ERASE_REGION", "Erase flash region"},
    [0xD2] = {"READ_FLASH", "Read flash"},
    [0xD3] = {"RUN_USER_CODE", "Run user code (soft reset)"},
    [0xD5] = {"SPI_NAND_ATTACH", "Attach SPI NAND flash"},
    [0xD6] = {"SPI_NAND_READ_SPARE", "Read NAND spare area"},
    [0xD7] = {"SPI_NAND_WRITE_SPARE", "Write NAND spare area"},
    [0xD8] = {"SPI_NAND_READ_FLASH", "Read NAND flash"},
    [0xD9] = {"SPI_NAND_WRITE_FLASH_BEGIN", "Begin NAND flash write"},
    [0xDA] = {"SPI_NAND_WRITE_FLASH_DATA", "NAND flash write data"},
    [0xDB] = {"SPI_NAND_ERASE_FLASH", "Erase entire NAND flash"},
    [0xDC] = {"SPI_NAND_ERASE_REGION", "Erase NAND flash region"},
    [0xDD] = {"SPI_NAND_READ_PAGE_DEBUG", "Read NAND page (debug)"},
    [0xDE] = {"SPI_NAND_WRITE_FLASH_END", "End NAND flash write"},
};

#define ESP_RESP_BUF_SIZE  8192

/* Get command name safely */
static const char *GetCmdName(BYTE cmd)
{
    const char *name = commandTable[cmd].name;
    return name ? name : "UNKNOWN";
}

/* SYNC response Val field prefix: {0x07, 0x07, 0x12}.
   The 4th byte (0x55) comes from request padding, not this array.
   Full response data is all zeros (4-byte status). */
static const BYTE sync_prefix[3] = { 0x07, 0x07, 0x12 };

BYTE Esptool_CalcChecksum(const BYTE *data, int len)
{
    BYTE sum = 0xEF;
    for (int i = 0; i < len; i++)
        sum ^= data[i];
    return sum;
}

/*
 * Defl_FreeBuffer - Free deflate accumulation buffer (without writing to flash)
 */
static void Defl_FreeBuffer(ESPTOOL_CTX *ctx)
{
    if (ctx->defl_buf) {
        HeapFree(GetProcessHeap(), 0, ctx->defl_buf);
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
static DWORD Esptool_EncryptInPlace(ESPTOOL_CTX *ctx, BYTE *data, DWORD len, DWORD flash_addr)
{
    if (!ctx->flash_encrypted)
        return ESP_OK;

    int key_len = 0;
    int key_offset = Chip_GetEncryptionKeyOffset(ctx->chip, &key_len);

    if (key_offset < 0 || !ctx->chip->efuse ||
        key_offset + key_len > ctx->chip->efuse_size) {
        TRACE_FW(TAG, "No encryption key available");
        Serial_PostLog(ctx->hNotify, L"ERR", L"  No encryption key in eFuse");
        return ESP_FAIL;
    }

    const BYTE *key = &ctx->chip->efuse[key_offset];
    ENCRYPT_CTX enc_ctx;
    int ret = Encrypt_Init(&enc_ctx, key, key_len, flash_addr);
    if (ret == ENCRYPT_OK)
        ret = Encrypt_Data(&enc_ctx, data, data, len);

    if (ret == ENCRYPT_OK) {
        TRACE_PROTO(TAG, "Encrypted %lu bytes at offset 0x%08lX", len, flash_addr);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Encrypted %lu bytes", len);
        return ESP_OK;
    }

    TRACE_FW(TAG, "Encryption failed: %d", ret);
    Serial_PostLogF(ctx->hNotify, L"ERR", L"  Encryption failed: %d", ret);
    return ESP_FAIL;
}

/* Flush deflate accumulation buffer: decompress and write to flash.
   Returns ESP_OK on success, ESP_FAIL on failure. */
static DWORD Defl_FlushBuffer(ESPTOOL_CTX *ctx)
{
    if (!ctx->defl_buf || ctx->defl_buf_size == 0 || ctx->defl_unc_size == 0) {
        Defl_FreeBuffer(ctx);
        return ESP_OK;
    }

    /* Allocate decompression buffer */
    BYTE *decomp_buf = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ctx->defl_unc_size);
    if (!decomp_buf) {
        TRACE_FW(TAG, "Failed to allocate decompression buffer");
        Serial_PostLog(ctx->hNotify, L"ERR", L"  Failed to allocate decompression buffer");
        Defl_FreeBuffer(ctx);
        return ESP_FAIL;
    }

    /* Initialize decompressor */
    DEFLATE_CTX deflate_ctx;
    deflate_init(&deflate_ctx, ctx->defl_buf, ctx->defl_buf_size,
                 decomp_buf, ctx->defl_unc_size);

    /* Decompress */
    int ret = deflate_decompress(&deflate_ctx);
    if (ret != DEFLATE_OK) {
        TRACE_FW(TAG, "Decompression failed: %d", ret);
        Serial_PostLogF(ctx->hNotify, L"ERR", L"  Decompression failed: %d", ret);
        HeapFree(GetProcessHeap(), 0, decomp_buf);
        Defl_FreeBuffer(ctx);
        return ESP_FAIL;
    }

    DWORD decomp_size = (DWORD)deflate_ctx.out_pos;
    TRACE_PROTO(TAG, "Defl flush: %lu -> %lu bytes at offset 0x%08lX",
                ctx->defl_buf_size, decomp_size, ctx->defl_offset);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  Decompressed %lu -> %lu bytes at offset 0x%08lX",
                    ctx->defl_buf_size, decomp_size, ctx->defl_offset);

    /* Encrypt if encrypted flag was set */
    if (Esptool_EncryptInPlace(ctx, decomp_buf, decomp_size, ctx->defl_offset) != ESP_OK) {
        HeapFree(GetProcessHeap(), 0, decomp_buf);
        Defl_FreeBuffer(ctx);
        return ESP_FAIL;
    }

    /* Write to flash */
    Flash_Write(ctx->flash, ctx->defl_offset, decomp_buf, decomp_size);

    HeapFree(GetProcessHeap(), 0, decomp_buf);
    Defl_FreeBuffer(ctx);
    return ESP_OK;
}

void Esptool_Init(ESPTOOL_CTX *ctx, CHIP_CTX *chip, FLASH_CTX *flash)
{
    memset(ctx, 0, sizeof(ESPTOOL_CTX));
    Slip_Init(&ctx->slip);
    ctx->chip = chip;
    ctx->flash = flash;
    ctx->state = ESP_STATE_IDLE;
    ctx->synced = FALSE;
    ctx->stub_mode = FALSE;
    ctx->hNotify = NULL;
    ctx->defl_buf = NULL;
    ctx->defl_buf_size = 0;
    ctx->defl_buf_cap = 0;
}

void Esptool_ResetState(ESPTOOL_CTX *ctx)
{
    /* Free deflate buffer without writing (reset scenario) */
    Defl_FreeBuffer(ctx);

    ctx->state = ESP_STATE_IDLE;
    ctx->synced = FALSE;
    ctx->stub_mode = FALSE;
    ctx->flash_offset = 0;
    ctx->flash_seq = 0;
    ctx->last_read_val = 0;
    ctx->flash_uncompressed_size = 0;
    ctx->defl_offset = 0;
    ctx->defl_unc_size = 0;
    ctx->flash_encrypted = FALSE;
    Slip_Reset(&ctx->slip);
    TRACE_PROTO(TAG, "Protocol state reset to IDLE");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Protocol state reset");
}

void Esptool_SetNotify(ESPTOOL_CTX *ctx, HWND hNotify)
{
    ctx->hNotify = hNotify;
}

void Esptool_SetModifiedCallback(ESPTOOL_CTX *ctx, ESP_MODIFIED_CB cb)
{
    ctx->onModified = cb;
}

void Esptool_SetWriteCallback(ESPTOOL_CTX *ctx, ESP_WRITE_CB cb)
{
    ctx->onWrite = cb;
}

void Esptool_SetBaudRateCallback(ESPTOOL_CTX *ctx, ESP_BAUDRATE_CB cb)
{
    ctx->onBaudRate = cb;
}

/*
 * Esptool_SendResponseEx - Send protocol response with configurable status length
 *
 * @ctx:        Protocol context
 * @cmd:        Command code (response will echo this)
 * @req_val:    Value field in response (usually last READ_REG value)
 * @status:     Status code (ESP_OK or ESP_FAIL)
 * @status_len: Status length in bytes (2 for stub, 4 for ROM)
 * @data:       Optional data payload (can be NULL)
 * @data_len:   Data payload length
 */
void Esptool_SendResponseEx(ESPTOOL_CTX *ctx, BYTE cmd, DWORD req_val, DWORD status, BYTE status_len, const BYTE *data, WORD data_len)
{
    BYTE resp[ESP_RESP_BUF_SIZE];
    int pos = 0;

    /* Calculate total size: header(8) + data_len */
    WORD total_data_len = data_len;

    TRACE_PROTO(TAG, "SendResponse cmd=0x%02X req_val=0x%08lX status=0x%08lX status_len=%u data_len=%u",
                cmd, req_val, status, status_len, data_len);

    /* Check if data fits in response buffer */
    if (data_len > sizeof(resp) - 8) {
        TRACE_FW(TAG, "Response too large: cmd=0x%02X data_len=%u max=%zu", cmd, data_len, sizeof(resp) - 8);
        Serial_PostLogF(ctx->hNotify, L"ERR", L"Response too large: cmd=0x%02X size=%u", cmd, data_len);
        return;
    }

    resp[pos++] = ESP_DIR_RESPONSE;
    resp[pos++] = cmd;
    resp[pos++] = (BYTE)(total_data_len & 0xFF);
    resp[pos++] = (BYTE)(total_data_len >> 8);
    resp[pos++] = (BYTE)(req_val & 0xFF);
    resp[pos++] = (BYTE)((req_val >> 8) & 0xFF);
    resp[pos++] = (BYTE)((req_val >> 16) & 0xFF);
    resp[pos++] = (BYTE)((req_val >> 24) & 0xFF);

    if (data && data_len > 0) {
        memcpy(&resp[pos], data, data_len);
    } else if (data_len > 0) {
        memset(&resp[pos], 0, data_len);
    }
    pos += data_len;

    /* SLIP encoding: worst case each byte needs escaping (2 bytes) + 2 frame markers */
    DWORD encoded_max = (DWORD)pos * 2 + 2;
    BYTE *encoded = (BYTE *)HeapAlloc(GetProcessHeap(), 0, encoded_max);
    if (!encoded) {
        TRACE_FW(TAG, "Failed to allocate encoded buffer (%lu bytes)", encoded_max);
        return;
    }

    int enc_len = Slip_Encode(resp, pos, encoded, encoded_max);
    if (enc_len > 0) {
        if (ctx->onWrite) {
            ctx->onWrite(encoded, (DWORD)enc_len);
        }
    }

    HeapFree(GetProcessHeap(), 0, encoded);

    const char *cmdName = GetCmdName(cmd);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"[RES] %hs size=%u status=0x%08lX",
                    cmdName, total_data_len, status);

    TRACE_PROTO(TAG, "TX cmd=0x%02X status=%lu len=%u", cmd, status, total_data_len);
}

void Esptool_SendResponse(ESPTOOL_CTX *ctx, BYTE cmd, DWORD req_val, DWORD status, const BYTE *data, WORD data_len)
{
    Esptool_SendResponseEx(ctx, cmd, req_val, status, 4, data, data_len);
}

static BOOL ParsePacket(const BYTE *frame, int frame_len, ESP_PACKET *pkt)
{
    if (frame_len < 8)
        return FALSE;

    pkt->direction = frame[0];
    pkt->command = frame[1];
    pkt->size = frame[2] | ((WORD)frame[3] << 8);
    pkt->value = frame[4] | ((DWORD)frame[5] << 8) |
                 ((DWORD)frame[6] << 16) | ((DWORD)frame[7] << 24);

    if (pkt->size > 0) {
        if (frame_len < 8 + pkt->size)
            return FALSE;
        if (pkt->size > sizeof(pkt->data))
            return FALSE;
        memcpy(pkt->data, &frame[8], pkt->size);
    }

    return TRUE;
}

static void HandleSync(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "SYNC received");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Sync handshake");
    ctx->state = ESP_STATE_SYNCED;
    ctx->synced = TRUE;
    ctx->stub_mode = FALSE;

    /* Real device returns sync sequence in Value field:
       {0x07, 0x07, 0x12, 0x55} as little-endian DWORD 0x55120707
       Note: The 4th byte is 0x55 (first padding byte from request), not 0x20 */
    DWORD sync_val = ((DWORD)sync_prefix[0]) |
                     ((DWORD)sync_prefix[1] << 8) |
                     ((DWORD)sync_prefix[2] << 16) |
                     ((DWORD)0x55 << 24);

    /* Real device sends 8 consecutive responses per SYNC request.
       Response format: Size=4, Data=4 bytes status (0x00000000) */
    for (int i = 0; i < ESP_SYNC_RESPONSE_COUNT; i++) {
        Esptool_SendResponseEx(ctx, ESP_CMD_SYNC, sync_val, ESP_OK, 4, NULL, 4);
    }
}

static void HandleReadReg(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                 ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD val = Chip_ReadReg(ctx->chip, addr);

    TRACE_PROTO(TAG, "READ_REG addr=0x%08lX val=0x%08lX", addr, val);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX -> 0x%08lX", addr, val);

    /* Cache the register value for use in subsequent responses */
    ctx->last_read_val = val;

    /* Transition to READY state when chip detection register is read */
    if (addr == CHIP_DETECT_REG && ctx->state == ESP_STATE_SYNCED) {
        ctx->state = ESP_STATE_READY;
        Serial_PostLog(ctx->hNotify, L"ESP", L"  Chip detected, ready for commands");
    }

    /* Real device returns register value in Value field (bytes 4-7),
       with status in Data field */
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_READ_REG, val, ESP_OK, status_len, NULL, status_len);
}

static void HandleWriteReg(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    /* WRITE_REG request format (16 bytes = 4 x 32-bit words):
       [addr:4][value:4][mask:4][delay_us:4] */
    DWORD addr = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                 ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD val = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD mask = 0xFFFFFFFF;
    DWORD delayUs = 0;

    if (pkt->size >= 12) {
        mask = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
               ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);
    }
    if (pkt->size >= 16) {
        delayUs = pkt->data[12] | ((DWORD)pkt->data[13] << 8) |
                  ((DWORD)pkt->data[14] << 16) | ((DWORD)pkt->data[15] << 24);
    }

    TRACE_PROTO(TAG, "WRITE_REG addr=0x%08lX val=0x%08lX mask=0x%08lX delay=%lu", addr, val, mask, delayUs);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX val=0x%08lX mask=0x%08lX delay=%lu", addr, val, mask, delayUs);
    Serial_PostLogF(ctx->hNotify, L"DBG", L"  WRITE_REG raw size=%u [%02X %02X %02X %02X %02X %02X %02X %02X]",
                    pkt->size,
                    pkt->data[0], pkt->data[1], pkt->data[2], pkt->data[3],
                    pkt->data[4], pkt->data[5], pkt->data[6], pkt->data[7]);

    /* Apply mask: only bits set in mask are written */
    DWORD currentVal = Chip_ReadReg(ctx->chip, addr);
    DWORD newVal = (currentVal & ~mask) | (val & mask);
    Chip_WriteReg(ctx->chip, addr, newVal);

    /* Debug: verify eFuse write by reading back */
    DWORD readback = Chip_ReadReg(ctx->chip, addr);
    Serial_PostLogF(ctx->hNotify, L"DBG", L"  WRITE_REG: current=0x%08lX val=0x%08lX mask=0x%08lX new=0x%08lX readback=0x%08lX",
                    currentVal, val, mask, newVal, readback);

    if (ctx->onModified) ctx->onModified();
    /* WRITE_REG always returns 2-byte status, Val field is 0x00000000 */
    Esptool_SendResponseEx(ctx, ESP_CMD_WRITE_REG, 0x00000000, ESP_OK, 2, NULL, 2);
}

static void HandleChangeBaudrate(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD new_baud = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                     ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD old_baud = 115200;

    if (pkt->size >= 8)
        old_baud = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                   ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "CHANGE_BAUDRATE old=%lu new=%lu", old_baud, new_baud);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  old=%lu new=%lu", old_baud, new_baud);

    /* Send response at old baud rate first */
    /* CHANGE_BAUDRATE always returns 2-byte status */
    Esptool_SendResponseEx(ctx, ESP_CMD_CHANGE_BAUDRATE, ctx->last_read_val, ESP_OK, 2, NULL, 2);

    /* Then switch to new baud rate */
    if (ctx->onBaudRate) {
        ctx->onBaudRate(new_baud);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Baud rate switched to %lu", new_baud);
    }
}

static void HandleMemBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD total = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                  ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD blocks = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                   ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD bsize = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                  ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);
    DWORD offset = pkt->data[12] | ((DWORD)pkt->data[13] << 8) |
                   ((DWORD)pkt->data[14] << 16) | ((DWORD)pkt->data[15] << 24);

    TRACE_PROTO(TAG, "MEM_BEGIN total=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                total, blocks, bsize, offset);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  total=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                    total, blocks, bsize, offset);

    ctx->state = ESP_STATE_MEM_WRITING;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_MEM_BEGIN, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleMemData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD seq = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "MEM_DATA seq=%lu len=%u", seq, pkt->size);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  seq=%lu len=%u", seq, pkt->size);

    /* Verify checksum: payload starts at offset 16 */
    if (pkt->size > 16) {
        const BYTE *payload = &pkt->data[16];
        int payload_len = pkt->size - 16;
        BYTE expected = Esptool_CalcChecksum(payload, payload_len);
        BYTE received = (BYTE)(pkt->value & 0xFF);
        if (expected != received) {
            TRACE_PROTO(TAG, "MEM_DATA checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            BYTE status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_MEM_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_MEM_DATA, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleMemEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD execute = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                    ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "MEM_END execute=%lu", execute);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  execute=%lu", execute);

    ctx->state = ESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_MEM_END, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);

    /* Send "OHAI" handshake after MEM_END to indicate stub is ready.
       Real device sends OHAI regardless of execute flag. */
    if (!ctx->stub_mode) {
        BYTE ohai[] = { 0xC0, 'O', 'H', 'A', 'I', 0xC0 };
        if (ctx->onWrite) {
            ctx->onWrite(ohai, sizeof(ohai));
        }
        ctx->stub_mode = TRUE;
        Serial_PostLog(ctx->hNotify, L"ESP", L"  Stub mode: OHAI sent");
        TRACE_PROTO(TAG, "Stub mode activated, OHAI sent");
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
    /* FLASH_DEFL_BEGIN format:
       [uncompressed_size:4][num_blocks:4][block_size:4][offset:4] */
    DWORD uncompressed_size = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                              ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD blocks = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                   ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD bsize = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                  ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);
    DWORD offset = pkt->data[12] | ((DWORD)pkt->data[13] << 8) |
                   ((DWORD)pkt->data[14] << 16) | ((DWORD)pkt->data[15] << 24);

    /* ROM mode sends extra 4 bytes for encrypted flag */
    DWORD encrypted = 0;
    if (pkt->size >= 20) {
        encrypted = pkt->data[16] | ((DWORD)pkt->data[17] << 8) |
                    ((DWORD)pkt->data[18] << 16) | ((DWORD)pkt->data[19] << 24);
    }

    /* Product mode: reject plaintext writes when encryption is active */
    if (!encrypted && Chip_IsFlashEncryptionEnabled(ctx->chip) &&
        Chip_IsDownloadEncryptDisabled(ctx->chip)) {
        TRACE_PROTO(TAG, "FLASH_DEFL_BEGIN rejected: production mode, plaintext not allowed");
        Serial_PostLog(ctx->hNotify, L"ERR", L"  Production mode: plaintext flash disabled");
        BYTE status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    TRACE_PROTO(TAG, "FLASH_DEFL_BEGIN uncompressed=%lu blocks=%lu bsize=%lu offset=0x%08lX encrypted=%lu",
                uncompressed_size, blocks, bsize, offset, encrypted);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  uncompressed=%lu blocks=%lu bsize=%lu offset=0x%08lX encrypted=%lu",
                    uncompressed_size, blocks, bsize, offset, encrypted);

    /* Flush any pending accumulated data from previous session */
    if (ctx->defl_buf && ctx->defl_buf_size > 0) {
        TRACE_PROTO(TAG, "FLASH_DEFL_BEGIN: flushing previous accumulation");
        Serial_PostLog(ctx->hNotify, L"ESP", L"  Flushing previous compressed data");
        DWORD ret = Defl_FlushBuffer(ctx);
        if (ret != ESP_OK) {
            TRACE_FW(TAG, "FLASH_DEFL_BEGIN flush previous failed");
            Serial_PostLog(ctx->hNotify, L"ERR", L"  Failed to flush previous compressed data");
            BYTE status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }
    } else {
        /* Free any leftover buffer */
        Defl_FreeBuffer(ctx);
    }

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
        if (ctx->onModified) ctx->onModified();
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Flash erased: offset=0x%08lX size=%lu", offset, uncompressed_size);
    }

    /* Allocate accumulation buffer */
    if (uncompressed_size > 0) {
        ctx->defl_buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, uncompressed_size);
        if (!ctx->defl_buf) {
            TRACE_FW(TAG, "Failed to allocate deflate buffer: %lu bytes", uncompressed_size);
            Serial_PostLogF(ctx->hNotify, L"ERR", L"  Failed to allocate deflate buffer: %lu bytes", uncompressed_size);
            BYTE status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }
        ctx->defl_buf_cap = uncompressed_size;
        ctx->defl_buf_size = 0;
        TRACE_PROTO(TAG, "FLASH_DEFL_BEGIN: allocated %lu bytes buffer", uncompressed_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

/*
 * HandleFlashDeflData - Handle compressed flash data block
 *
 * FLASH_DEFL_DATA (0x11) receives a block of compressed data.
 * Data is accumulated in the deflate buffer for later decompression.
 *
 * Request format: [data_len:4][seq:4][padding:4][padding:4][compressed_data:data_len]
 *
 * The sequence number must match the expected value, and the checksum
 * (XOR of payload) is verified before accumulating the data.
 */
static void HandleFlashDeflData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD data_len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                     ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD seq = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "FLASH_DEFL_DATA seq=%lu len=%lu", seq, data_len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  seq=%lu len=%lu", seq, data_len);

    /* Verify sequence number */
    if (seq != ctx->flash_seq) {
        TRACE_PROTO(TAG, "FLASH_DEFL_DATA seq mismatch: expected=%lu received=%lu", ctx->flash_seq, seq);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Seq mismatch: expected=%lu received=%lu", ctx->flash_seq, seq);
        BYTE status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    if (pkt->size >= 16 && data_len <= (DWORD)(pkt->size - 16)) {
        const BYTE *payload = &pkt->data[16];

        /* Verify checksum */
        BYTE expected = Esptool_CalcChecksum(payload, (int)data_len);
        BYTE received = (BYTE)(pkt->value & 0xFF);
        if (expected != received) {
            TRACE_PROTO(TAG, "FLASH_DEFL_DATA checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            BYTE status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }

        /* Accumulate compressed data into buffer */
        if (ctx->defl_buf_cap > 0 && data_len > 0) {
            /* Check buffer overflow */
            if (ctx->defl_buf_size + data_len > ctx->defl_buf_cap) {
                TRACE_FW(TAG, "Deflate buffer overflow: size=%lu cap=%lu add=%lu",
                         ctx->defl_buf_size, ctx->defl_buf_cap, data_len);
                Serial_PostLogF(ctx->hNotify, L"ERR", L"  Deflate buffer overflow");
                Defl_FreeBuffer(ctx);
                BYTE status_len = ESP_STATUS_LEN(ctx);
                Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
                return;
            }

            memcpy(ctx->defl_buf + ctx->defl_buf_size, payload, data_len);
            ctx->defl_buf_size += data_len;

            TRACE_PROTO(TAG, "FLASH_DEFL_DATA accumulated %lu/%lu bytes", ctx->defl_buf_size, ctx->defl_buf_cap);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Accumulated %lu/%lu bytes", ctx->defl_buf_size, ctx->defl_buf_cap);
        }

        ctx->flash_seq = seq + 1;
    }

    if (ctx->onModified) ctx->onModified();
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
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
    TRACE_PROTO(TAG, "FLASH_DEFL_END");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  End compressed flash download");

    /* Decompress accumulated data and write to flash */
    DWORD ret = Defl_FlushBuffer(ctx);
    if (ret != ESP_OK) {
        TRACE_FW(TAG, "FLASH_DEFL_END flush failed");
        Serial_PostLog(ctx->hNotify, L"ERR", L"  Decompression flush failed");
        ctx->state = ESP_STATE_READY;
        BYTE status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_END, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    ctx->state = ESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_END, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
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
    DWORD addr = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                 ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD len = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD bsize = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                  ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);
    DWORD psize = pkt->data[12] | ((DWORD)pkt->data[13] << 8) |
                  ((DWORD)pkt->data[14] << 16) | ((DWORD)pkt->data[15] << 24);

    (void)psize;

    TRACE_PROTO(TAG, "READ_FLASH addr=0x%08lX len=%lu bsize=%lu psize=%lu", addr, len, bsize, psize);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX len=%lu bsize=%lu", addr, len, bsize);

    /* Step 1: Send command ACK (2-byte status in command response) */
    Esptool_SendResponseEx(ctx, ESP_CMD_READ_FLASH, ctx->last_read_val, ESP_OK, 2, NULL, 2);

    /* Step 2: Send flash data as separate SLIP frames */
    DWORD offset = 0;
    while (offset < len) {
        DWORD chunk_size = len - offset;
        if (chunk_size > bsize)
            chunk_size = bsize;

        /* Read flash data into temporary buffer */
        BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, chunk_size);
        if (!buf) {
            Serial_PostLogF(ctx->hNotify, L"ERR", L"  Failed to allocate read buffer: %lu", chunk_size);
            return;
        }

        if (!Flash_Read(ctx->flash, addr + offset, buf, chunk_size)) {
            Serial_PostLogF(ctx->hNotify, L"ERR", L"  Flash read failed at offset 0x%08lX", addr + offset);
            HeapFree(GetProcessHeap(), 0, buf);
            return;
        }

        /* SLIP-encode the chunk and send as raw frame (not a command response) */
        DWORD encoded_max = (DWORD)chunk_size * 2 + 2;
        BYTE *encoded = (BYTE *)HeapAlloc(GetProcessHeap(), 0, encoded_max);
        if (!encoded) {
            Serial_PostLogF(ctx->hNotify, L"ERR", L"  Failed to allocate encode buffer");
            HeapFree(GetProcessHeap(), 0, buf);
            return;
        }

        int enc_len = Slip_Encode(buf, (int)chunk_size, encoded, (int)encoded_max);
        if (enc_len > 0 && ctx->onWrite) {
            ctx->onWrite(encoded, (DWORD)enc_len);
        }

        HeapFree(GetProcessHeap(), 0, encoded);
        HeapFree(GetProcessHeap(), 0, buf);

        offset += chunk_size;
    }

    /* Step 3: Calculate and send 16-byte MD5 digest as final SLIP frame */
    BYTE md5[16];
    Flash_CalcMd5(ctx->flash, addr, len, md5);

    BYTE md5_encoded[34];  /* 16 bytes * 2 (worst case escaping) + 2 (framing) */
    int md5_enc_len = Slip_Encode(md5, 16, md5_encoded, sizeof(md5_encoded));
    if (md5_enc_len > 0 && ctx->onWrite) {
        ctx->onWrite(md5_encoded, (DWORD)md5_enc_len);
    }

    TRACE_PROTO(TAG, "READ_FLASH complete: %lu bytes sent", len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  Read complete: %lu bytes", len);
}

/*
 * HandleEraseFlash - Handle erase entire flash command (stub-only)
 *
 * ERASE_FLASH (0xD0) erases the entire flash memory.
 * Also frees any pending deflate buffer.
 */
static void HandleEraseFlash(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "ERASE_FLASH");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Erase entire flash");

    /* Free any pending deflate buffer */
    Defl_FreeBuffer(ctx);

    Flash_EraseAll(ctx->flash);
    if (ctx->onModified) ctx->onModified();
    /* ERASE_FLASH is stub-only, always returns 2-byte status */
    Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_FLASH, ctx->last_read_val, ESP_OK, 2, NULL, 2);
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
    DWORD offset = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD len = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "ERASE_BLOCK offset=0x%08lX len=%lu", offset, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  offset=0x%08lX len=%lu", offset, len);

    /* Free any pending deflate buffer */
    Defl_FreeBuffer(ctx);

    /* ERASE_REGION is stub-only, always returns 2-byte status */
    if (Flash_Erase(ctx->flash, offset, len)) {
        if (ctx->onModified) ctx->onModified();
        Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_REGION, ctx->last_read_val, ESP_OK, 2, NULL, 2);
    } else {
        Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_REGION, ctx->last_read_val, ESP_FAIL, 2, NULL, 2);
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
    DWORD addr = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                 ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD len = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "FLASH_MD5 addr=0x%08lX len=%lu", addr, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX len=%lu", addr, len);

    BYTE md5[16];
    Flash_CalcMd5(ctx->flash, addr, len, md5);

    /* Response format: [data:N][status:2]
       ROM mode:   [md5_hex:32][status:2] = 34 bytes
       Stub mode:  [md5_bin:16][status:2] = 18 bytes */
    if (ctx->stub_mode) {
        /* Stub mode: return 16-byte binary MD5 + 2-byte status */
        BYTE resp[18];
        memcpy(&resp[0], md5, 16);
        resp[16] = 0x00;  /* status byte 1 (success) */
        resp[17] = 0x00;  /* status byte 2 (success) */
        TRACE_PROTO(TAG, "  MD5 (stub, binary)");
        Serial_PostLog(ctx->hNotify, L"ESP", L"  MD5 (stub, binary)");
        Esptool_SendResponseEx(ctx, ESP_CMD_SPI_FLASH_MD5, ctx->last_read_val, ESP_OK, 2, resp, 18);
    } else {
        /* ROM mode: return 32-byte ASCII hex MD5 + 2-byte status */
        BYTE resp[34];
        for (int i = 0; i < 16; i++)
            sprintf((char *)&resp[i * 2], "%02x", md5[i]);
        resp[32] = 0x00;  /* status byte 1 (success) */
        resp[33] = 0x00;  /* status byte 2 (success) */
        TRACE_PROTO(TAG, "  MD5=%.*s", 32, resp);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  MD5=%.*hs", 32, resp);
        Esptool_SendResponseEx(ctx, ESP_CMD_SPI_FLASH_MD5, ctx->last_read_val, ESP_OK, 2, resp, 34);
    }
}

/*
 * HandleFlashBegin - Handle flash write begin command
 *
 * FLASH_BEGIN (0x02) starts a flash write session.
 * Erases the specified flash region and prepares for receiving data blocks.
 *
 * Request format (stub): [erase_size:4][num_blocks:4][block_size:4][offset:4]
 * Request format (ROM):  [erase_size:4][num_blocks:4][block_size:4][offset:4][encrypted:4]
 *
 * Note: Does NOT free deflate buffer, as client may send FLASH_BEGIN
 * before FLASH_DEFL_END in some scenarios.
 */
static void HandleFlashBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD erase_size = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                       ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD num_blocks = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                       ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD block_size = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                       ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);
    DWORD offset = pkt->data[12] | ((DWORD)pkt->data[13] << 8) |
                   ((DWORD)pkt->data[14] << 16) | ((DWORD)pkt->data[15] << 24);

    /* ROM mode sends extra 4 bytes for encrypted flag */
    DWORD encrypted = 0;
    if (pkt->size >= 20) {
        encrypted = pkt->data[16] | ((DWORD)pkt->data[17] << 8) |
                    ((DWORD)pkt->data[18] << 16) | ((DWORD)pkt->data[19] << 24);
    }

    /* Product mode: reject plaintext writes when encryption is active */
    if (!encrypted && Chip_IsFlashEncryptionEnabled(ctx->chip) &&
        Chip_IsDownloadEncryptDisabled(ctx->chip)) {
        TRACE_PROTO(TAG, "FLASH_BEGIN rejected: production mode, plaintext not allowed");
        Serial_PostLog(ctx->hNotify, L"ERR", L"  Production mode: plaintext flash disabled");
        BYTE status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_BEGIN, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    /* Note: Do NOT free deflate buffer here.
       Client may send FLASH_BEGIN before FLASH_DEFL_END.
       The buffer will be flushed by HandleFlashDeflEnd. */

    ctx->flash_offset = offset;
    ctx->flash_seq = 0;
    ctx->flash_encrypted = (encrypted != 0);
    ctx->state = ESP_STATE_FLASH_WRITING;

    TRACE_PROTO(TAG, "FLASH_BEGIN erase=%lu blocks=%lu bsize=%lu offset=0x%08lX encrypted=%lu",
                erase_size, num_blocks, block_size, offset, encrypted);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  erase=%lu blocks=%lu bsize=%lu offset=0x%08lX encrypted=%lu",
                    erase_size, num_blocks, block_size, offset, encrypted);

    /* Erase the flash region as requested by the host */
    if (erase_size > 0) {
        Flash_Erase(ctx->flash, offset, erase_size);
        if (ctx->onModified) ctx->onModified();
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Flash erased: offset=0x%08lX size=%lu", offset, erase_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_BEGIN, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
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
    DWORD data_len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                     ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD seq = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "FLASH_DATA seq=%lu len=%lu", seq, data_len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  seq=%lu len=%lu", seq, data_len);

    /* Verify sequence number */
    if (seq != ctx->flash_seq) {
        TRACE_PROTO(TAG, "FLASH_DATA seq mismatch: expected=%lu received=%lu", ctx->flash_seq, seq);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Seq mismatch: expected=%lu received=%lu", ctx->flash_seq, seq);
        BYTE status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    if (pkt->size >= 16 && data_len <= (DWORD)(pkt->size - 16)) {
        const BYTE *payload = &pkt->data[16];

        /* Verify checksum */
        BYTE expected = Esptool_CalcChecksum(payload, (int)data_len);
        BYTE received = (BYTE)(pkt->value & 0xFF);
        if (expected != received) {
            TRACE_PROTO(TAG, "FLASH_DATA checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            BYTE status_len = ESP_STATUS_LEN(ctx);
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }

        /* Encrypt if encrypted flag was set */
        if (ctx->flash_encrypted) {
            int key_len = 0;
            int key_offset = Chip_GetEncryptionKeyOffset(ctx->chip, &key_len);

            if (key_offset >= 0 && ctx->chip->efuse &&
                key_offset + key_len <= ctx->chip->efuse_size) {
                const BYTE *key = &ctx->chip->efuse[key_offset];
                BYTE *enc_buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, data_len);
                if (enc_buf) {
                    ENCRYPT_CTX enc_ctx;
                    int ret = Encrypt_Init(&enc_ctx, key, key_len, ctx->flash_offset);
                    if (ret == ENCRYPT_OK)
                        ret = Encrypt_Data(&enc_ctx, payload, enc_buf, data_len);
                    if (ret == ENCRYPT_OK) {
                        Flash_Write(ctx->flash, ctx->flash_offset, enc_buf, data_len);
                        TRACE_PROTO(TAG, "Encrypted %lu bytes at offset 0x%08lX", data_len, ctx->flash_offset);
                    } else {
                        Flash_Write(ctx->flash, ctx->flash_offset, payload, data_len);
                        TRACE_FW(TAG, "Encryption failed: %d", ret);
                    }
                    HeapFree(GetProcessHeap(), 0, enc_buf);
                } else {
                    Flash_Write(ctx->flash, ctx->flash_offset, payload, data_len);
                }
            } else {
                Flash_Write(ctx->flash, ctx->flash_offset, payload, data_len);
                TRACE_FW(TAG, "No encryption key available");
            }
        } else {
            Flash_Write(ctx->flash, ctx->flash_offset, payload, data_len);
        }

        ctx->flash_offset += data_len;
        ctx->flash_seq = seq + 1;
    }

    if (ctx->onModified) ctx->onModified();
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

/*
 * HandleFlashEnd - Handle flash write end command
 *
 * FLASH_END (0x04) completes a flash write session.
 * Optionally triggers a soft reboot.
 *
 * Request format: [reboot:4] (0=don't reboot, 1=reboot)
 *
 * Also frees any pending deflate buffer (mode switch scenario).
 */
static void HandleFlashEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD reboot = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "FLASH_END reboot=%lu", reboot);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  reboot=%lu", reboot);

    /* Free any pending deflate buffer (mode switch) */
    Defl_FreeBuffer(ctx);

    ctx->state = ESP_STATE_READY;

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_END, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
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
    TRACE_PROTO(TAG, "GET_SECURITY_INFO");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Get security info");

    /* ESP8266 ROM does not support GET_SECURITY_INFO.
       Return ROM_INVALID_RECV_MSG error so esptool falls back to magic value.
       ESP32 ROM also doesn't support it, but stub does. */
    if (ctx->chip->type == CHIP_ESP8266) {
        TRACE_PROTO(TAG, "  Not supported on %s, returning error", ctx->chip->name);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Not supported on %hs", ctx->chip->name);
        /* Status = ROM_INVALID_RECV_MSG (0x05), status_len=4 for ROM mode */
        Esptool_SendResponseEx(ctx, ESP_CMD_GET_SECURITY_INFO, ctx->last_read_val, 0x05, 4, NULL, 4);
        return;
    }

    /* ESP32 ROM mode: return error so esptool falls back to eFuse register reading.
       Stub mode: return proper security info. */
    if (ctx->chip->type == CHIP_ESP32 && !ctx->stub_mode) {
        TRACE_PROTO(TAG, "  ESP32 ROM: not supported, returning error");
        Serial_PostLog(ctx->hNotify, L"ESP", L"  ESP32 ROM: not supported");
        Esptool_SendResponseEx(ctx, ESP_CMD_GET_SECURITY_INFO, ctx->last_read_val, 0x05, 4, NULL, 4);
        return;
    }

    /* ESP32-S2: Return 14-byte response (12 payload + 2 status, no chip_id).
       esptool tries resp_data_len=20 first (fails), then resp_data_len=12 (succeeds).
       chip_id will be None, causing get_chip_id() to raise FatalError,
       which triggers fallback to magic value detection. */
    if (ctx->chip->type == CHIP_ESP32S2) {
        TRACE_PROTO(TAG, "  ESP32-S2: returning 14-byte response (no chip_id)");
        Serial_PostLog(ctx->hNotify, L"ESP", L"  ESP32-S2: no chip_id in response");
        DWORD flash_crypt_cnt = Chip_GetFlashCryptCnt(ctx->chip);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  flags=0x%08lX flash_crypt_cnt=%u",
                        0UL, (unsigned)flash_crypt_cnt);
        BYTE sec_data[14] = {0};
        /* bytes 0-3:   flags (all zeros) */
        /* byte 4:      flash_crypt_cnt */
        sec_data[4] = (BYTE)(flash_crypt_cnt & 0xFF);
        /* bytes 5-11:  key_purposes (7 bytes, one per key block KEY0-KEY5 + reserved) */
        for (int i = 0; i < 7; i++) {
            sec_data[5 + i] = Chip_GetKeyPurpose(ctx->chip, i);
        }
        /* bytes 12-13: status = success (0x00, 0x00) */
        Esptool_SendResponse(ctx, ESP_CMD_GET_SECURITY_INFO, ctx->last_read_val, ESP_OK, sec_data, 14);
        return;
    }

    /* ESP32 stub, ESP32-S3, C2, C3, C6: Return 22-byte response with IMAGE_CHIP_ID.
       [flags:4][flash_crypt_cnt:1][key_purposes:7][chip_id:4][api_version:4][status:2]
       For ESP32 stub, chip_id = EFUSE_CHIP_ID (0x00F01D83). */
    DWORD chip_id = (ctx->chip->type == CHIP_ESP32) ? ctx->chip->chip_id : ctx->chip->security_chip_id;
    DWORD flash_crypt_cnt = Chip_GetFlashCryptCnt(ctx->chip);

    BYTE sec_data[22] = {0};
    /* bytes 0-3:   flags (all zeros) */
    /* byte 4:      flash_crypt_cnt */
    sec_data[4] = (BYTE)(flash_crypt_cnt & 0xFF);
    /* bytes 5-11:  key_purposes (7 bytes, one per key block KEY0-KEY5 + reserved) */
    for (int i = 0; i < 7; i++) {
        sec_data[5 + i] = Chip_GetKeyPurpose(ctx->chip, i);
    }
    /* bytes 12-15: chip_id (IMAGE_CHIP_ID, little-endian) */
    sec_data[12] = (BYTE)(chip_id & 0xFF);
    sec_data[13] = (BYTE)((chip_id >> 8) & 0xFF);
    sec_data[14] = (BYTE)((chip_id >> 16) & 0xFF);
    sec_data[15] = (BYTE)((chip_id >> 24) & 0xFF);
    /* bytes 16-19: api_version (0) */
    /* bytes 20-21: status = success (0x00, 0x00) */

    TRACE_PROTO(TAG, "  chip_id (IMAGE_CHIP_ID)=%lu (0x%08lX)", chip_id, chip_id);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  flags=0x%08lX flash_crypt_cnt=%u",
                    0UL, (unsigned)flash_crypt_cnt);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  chip_id=%lu (0x%08lX) api_version=%lu",
                    chip_id, chip_id, 0UL);

    /* Transition to READY state when chip detection succeeds via GET_SECURITY_INFO */
    if (ctx->state == ESP_STATE_SYNCED) {
        ctx->state = ESP_STATE_READY;
        Serial_PostLog(ctx->hNotify, L"ESP", L"  Chip detected via security info, ready for commands");
    }

    Esptool_SendResponse(ctx, ESP_CMD_GET_SECURITY_INFO, ctx->last_read_val, ESP_OK, sec_data, 22);
}

/*
 * HandleSpiAttach - Handle SPI flash attach command
 *
 * SPI_ATTACH (0x0D) initializes the SPI flash controller.
 * In the simulator, this is a no-op that always succeeds.
 */
static void HandleSpiAttach(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "SPI_ATTACH");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Attach SPI flash");

    /* SPI_ATTACH: ROM mode 4-byte status, stub mode 2-byte status */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_SPI_ATTACH, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
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
    DWORD fl_id = 0, total_size = 0, block_size = 0, sector_size = 0, page_size = 0, status_mask = 0;

    if (pkt->size >= 24) {
        fl_id = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
        total_size = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                     ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
        block_size = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                     ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);
        sector_size = pkt->data[12] | ((DWORD)pkt->data[13] << 8) |
                      ((DWORD)pkt->data[14] << 16) | ((DWORD)pkt->data[15] << 24);
        page_size = pkt->data[16] | ((DWORD)pkt->data[17] << 8) |
                    ((DWORD)pkt->data[18] << 16) | ((DWORD)pkt->data[19] << 24);
        status_mask = pkt->data[20] | ((DWORD)pkt->data[21] << 8) |
                      ((DWORD)pkt->data[22] << 16) | ((DWORD)pkt->data[23] << 24);
    }

    TRACE_PROTO(TAG, "SPI_SET_PARAMS fl_id=0x%08lX total=%lu block=%lu sector=%lu page=%lu mask=0x%08lX",
                fl_id, total_size, block_size, sector_size, page_size, status_mask);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  fl_id=0x%08lX total=%lu block=%lu sector=%lu page=%lu mask=0x%08lX",
                    fl_id, total_size, block_size, sector_size, page_size, status_mask);

    /* ESP8266 ROM does not support SPI_SET_PARAMS.
       Return ROM_INVALID_RECV_MSG error so esptool falls back gracefully. */
    if (ctx->chip->type == CHIP_ESP8266 && !ctx->stub_mode) {
        TRACE_PROTO(TAG, "  Not supported on ESP8266 ROM, returning error");
        Serial_PostLog(ctx->hNotify, L"ESP", L"  Not supported on ESP8266 ROM");
        BYTE err_data[4] = {0x01, 0x05, 0x00, 0x00};
        Esptool_SendResponse(ctx, ESP_CMD_SPI_SET_PARAMS, ctx->last_read_val, ESP_OK, err_data, 4);
        return;
    }

    /* ESP32+ ROM and all stubs: return success.
       ROM mode: 4-byte status; stub mode: 2-byte status. */
    BYTE status_len = ESP_STATUS_LEN(ctx);
    Esptool_SendResponseEx(ctx, ESP_CMD_SPI_SET_PARAMS, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
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
    TRACE_PROTO(TAG, "RUN_USER_CODE");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Run user code (soft reset)");

    /* RUN_USER_CODE: stub-only, fire-and-forget (client does not wait for response) */
    Esptool_SendResponseEx(ctx, ESP_CMD_RUN_USER_CODE, ctx->last_read_val, ESP_OK, 2, NULL, 2);

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
 * Returns TRUE if frame was processed successfully, FALSE on error.
 */
BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len)
{
    ESP_PACKET *pkt = &ctx->pkt;

    TRACE_PROTO(TAG, "RX frame len=%d", frame_len);

    if (!ParsePacket(frame, frame_len, pkt)) {
        TRACE_FW(TAG, "Invalid packet");
        return FALSE;
    }

    /* Get command name */
    const char *cmdName = GetCmdName(pkt->command);

    /* Get direction string */
    const WCHAR *dirStr = (pkt->direction == ESP_DIR_REQUEST) ? L"REQ" : L"RES";

    /* Log packet summary */
    Serial_PostLogF(ctx->hNotify, L"ESP", L"[%s] %hs size=%u val=0x%08lX",
                    dirStr, cmdName, pkt->size, pkt->value);

    if (pkt->direction != ESP_DIR_REQUEST) {
        TRACE_FW(TAG, "Not a request: 0x%02X", pkt->direction);
        return FALSE;
    }

    /* State validation: check if command is allowed in current state */
    BOOL valid = TRUE;
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
            valid = FALSE;
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
            valid = FALSE;
        }
        break;

    /* Flash data commands require FLASH_WRITING state */
    case ESP_CMD_FLASH_DATA:
    case ESP_CMD_FLASH_END:
    case ESP_CMD_FLASH_DEFL_DATA:
    case ESP_CMD_FLASH_DEFL_END:
        if (ctx->state != ESP_STATE_FLASH_WRITING) {
            valid = FALSE;
        }
        break;

    /* Memory data commands require MEM_WRITING state */
    case ESP_CMD_MEM_DATA:
    case ESP_CMD_MEM_END:
        if (ctx->state != ESP_STATE_MEM_WRITING) {
            valid = FALSE;
        }
        break;

    /* NAND flash commands (0xD5-0xDE) - not implemented in FakeEsptool.
       These commands are stub-only and used by ESP32-S3 etc. for NAND flash.
       Listed here explicitly for documentation; they will return ROM_INVALID_RECV_MSG. */
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
        valid = FALSE;
        break;
    }

    if (!valid) {
        TRACE_PROTO(TAG, "Command 0x%02X not allowed in state %d", pkt->command, ctx->state);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Command 0x%02X rejected (state=%d)",
                        pkt->command, ctx->state);
        Esptool_SendResponse(ctx, pkt->command, pkt->value, ESP_FAIL, NULL, 4);
        return FALSE;
    }

    switch (pkt->command) {
    case ESP_CMD_SYNC:              HandleSync(ctx, pkt); break;
    case ESP_CMD_READ_REG:          HandleReadReg(ctx, pkt); break;
    case ESP_CMD_WRITE_REG:         HandleWriteReg(ctx, pkt); break;
    case ESP_CMD_SPI_SET_PARAMS:    HandleSpiSetParams(ctx, pkt); break;
    case ESP_CMD_SPI_ATTACH:        HandleSpiAttach(ctx, pkt); break;
    case ESP_CMD_CHANGE_BAUDRATE:   HandleChangeBaudrate(ctx, pkt); break;
    case ESP_CMD_FLASH_BEGIN:       HandleFlashBegin(ctx, pkt); break;
    case ESP_CMD_FLASH_DATA:        HandleFlashData(ctx, pkt); break;
    case ESP_CMD_FLASH_END:         HandleFlashEnd(ctx, pkt); break;
    case ESP_CMD_MEM_BEGIN:         HandleMemBegin(ctx, pkt); break;
    case ESP_CMD_MEM_DATA:          HandleMemData(ctx, pkt); break;
    case ESP_CMD_MEM_END:           HandleMemEnd(ctx, pkt); break;
    case ESP_CMD_FLASH_DEFL_BEGIN:  HandleFlashDeflBegin(ctx, pkt); break;
    case ESP_CMD_FLASH_DEFL_DATA:   HandleFlashDeflData(ctx, pkt); break;
    case ESP_CMD_FLASH_DEFL_END:    HandleFlashDeflEnd(ctx, pkt); break;
    case ESP_CMD_SPI_FLASH_MD5:     HandleFlashMd5(ctx, pkt); break;
    case ESP_CMD_ERASE_FLASH:       HandleEraseFlash(ctx, pkt); break;
    case ESP_CMD_ERASE_REGION:      HandleEraseBlock(ctx, pkt); break;
    case ESP_CMD_READ_FLASH:        HandleReadFlash(ctx, pkt); break;
    case ESP_CMD_GET_SECURITY_INFO: HandleGetSecurityInfo(ctx, pkt); break;
    case ESP_CMD_RUN_USER_CODE:     HandleRunUserCode(ctx, pkt); break;
    default:
        /* ROM returns ROM_INVALID_RECV_MSG (0x05) for unsupported commands */
        TRACE_FW(TAG, "Unknown cmd: 0x%02X", pkt->command);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Unknown command: 0x%02X", pkt->command);
        {
            /* Response format: [status_byte_1 != 0][ROM_INVALID_RECV_MSG] + padding */
            BYTE err_data[4] = {0x01, 0x05, 0x00, 0x00};
            Esptool_SendResponse(ctx, pkt->command, pkt->value, ESP_OK, err_data, 4);
        }
        return FALSE;
    }

    return TRUE;
}

BOOL Esptool_Feed(ESPTOOL_CTX *ctx, const BYTE *data, int len)
{
    BOOL got_frame = FALSE;

    for (int i = 0; i < len; i++) {
        if (Slip_PutByte(&ctx->slip, data[i])) {
            const BYTE *payload = Slip_GetPayload(&ctx->slip);
            int plen = Slip_GetLength(&ctx->slip);

            if (plen >= 8) {
                Esptool_ProcessFrame(ctx, payload, plen);
                got_frame = TRUE;
            }

            Slip_Reset(&ctx->slip);
        }
    }

    return got_frame;
}
