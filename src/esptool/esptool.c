/*
 * esptool.c - esptool protocol handler implementation
 *
 * Parses SLIP frames, routes commands, and sends responses.
 */

#include "esptool.h"
#include "../serial.h"
#include "../utils/trace.h"
#include "../utils/deflate.h"
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
};

#define ESP_RESP_BUF_SIZE  8192

static const BYTE sync_response[ESP_SYNC_SEQ_LEN] = {
    0x07, 0x07, 0x12, 0x20,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55
};

BYTE Esptool_CalcChecksum(const BYTE *data, int len)
{
    BYTE sum = 0xEF;
    for (int i = 0; i < len; i++)
        sum ^= data[i];
    return sum;
}

void Esptool_Init(ESPTOOL_CTX *ctx)
{
    memset(ctx, 0, sizeof(ESPTOOL_CTX));
    Slip_Init(&ctx->slip);
    Chip_Init(&ctx->chip, CHIP_ESP32);
    Flash_Init(&ctx->flash, 4 * 1024 * 1024);
    ctx->synced = FALSE;
    ctx->stub_mode = FALSE;
    ctx->hNotify = NULL;
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

void Esptool_SetChipType(ESPTOOL_CTX *ctx, CHIP_TYPE type)
{
    Flash_Close(&ctx->flash);
    Chip_Close(&ctx->chip);
    Chip_Init(&ctx->chip, type);
    Flash_Init(&ctx->flash, ctx->chip.flash_size);
}

void Esptool_SetFlashSize(ESPTOOL_CTX *ctx, DWORD size)
{
    Flash_Close(&ctx->flash);
    Chip_SetFlashSize(&ctx->chip, size);
    Flash_Init(&ctx->flash, size);
}

void Esptool_SendResponseEx(ESPTOOL_CTX *ctx, BYTE cmd, DWORD req_val, DWORD status, BYTE status_len, const BYTE *data, WORD data_len)
{
    BYTE resp[ESP_RESP_BUF_SIZE];
    int pos = 0;

    /* Calculate total size: header(8) + data_len */
    WORD total_data_len = data_len;

    TRACE_PROTO(TAG, "SendResponse cmd=0x%02X req_val=0x%08lX status=0x%08lX status_len=%u data_len=%u",
                cmd, req_val, status, status_len, data_len);

    Serial_PostLogF(ctx->hNotify, L"DBG", L"SendResponse cmd=0x%02X status_len=%u data_len=%u",
                    cmd, status_len, data_len);

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
    }
    pos += data_len;

    BYTE encoded[ESP_RESP_BUF_SIZE * 3];
    int enc_len = Slip_Encode(resp, pos, encoded, sizeof(encoded));
    if (enc_len > 0) {
        if (ctx->onWrite) {
            ctx->onWrite(encoded, (DWORD)enc_len);
        }
    }

    const char *cmdName = commandTable[cmd].name;
    if (!cmdName)
        cmdName = "UNKNOWN";
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
    ctx->synced = TRUE;
    ctx->stub_mode = FALSE;

    /* Real device returns sync sequence header {0x07,0x07,0x12,0x20}
       as the Value field (little-endian DWORD 0x20120707) */
    DWORD sync_val = ((DWORD)sync_response[0]) |
                     ((DWORD)sync_response[1] << 8) |
                     ((DWORD)sync_response[2] << 16) |
                     ((DWORD)sync_response[3] << 24);
    BYTE sync_data[4] = {0x00, 0x00, 0x00, 0x00};

    /* Real device sends 8 consecutive responses per SYNC request */
    for (int i = 0; i < 8; i++) {
        Esptool_SendResponse(ctx, ESP_CMD_SYNC, sync_val, ESP_OK, sync_data, 4);
    }
}

static void HandleReadReg(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                 ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD val = Chip_ReadReg(&ctx->chip, addr);

    TRACE_PROTO(TAG, "READ_REG addr=0x%08lX val=0x%08lX", addr, val);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX -> 0x%08lX", addr, val);

    /* Cache the register value for use in subsequent responses */
    ctx->last_read_val = val;

    /* Real device returns register value in Value field (bytes 4-7),
       with status in Data field */
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
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

    /* Apply mask: only bits set in mask are written */
    DWORD currentVal = Chip_ReadReg(&ctx->chip, addr);
    DWORD newVal = (currentVal & ~mask) | (val & mask);
    Chip_WriteReg(&ctx->chip, addr, newVal);

    if (ctx->onModified) ctx->onModified();
    /* WRITE_REG always returns 2-byte status */
    Esptool_SendResponseEx(ctx, ESP_CMD_WRITE_REG, ctx->last_read_val, ESP_OK, 2, NULL, 2);
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

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
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
            BYTE status_len = ctx->stub_mode ? 2 : 4;
            Esptool_SendResponseEx(ctx, ESP_CMD_MEM_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_MEM_DATA, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleMemEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD execute = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                    ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "MEM_END execute=%lu", execute);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  execute=%lu", execute);
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
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

    ctx->flash_offset = offset;
    ctx->flash_seq = 0;
    ctx->flash_uncompressed_size = uncompressed_size;

    TRACE_PROTO(TAG, "FLASH_DEFL_BEGIN uncompressed=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                uncompressed_size, blocks, bsize, offset);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  uncompressed=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                    uncompressed_size, blocks, bsize, offset);

    /* Erase the flash region (use uncompressed_size for erase calculation) */
    if (uncompressed_size > 0) {
        Flash_Erase(&ctx->flash, offset, uncompressed_size);
        if (ctx->onModified) ctx->onModified();
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Flash erased: offset=0x%08lX size=%lu", offset, uncompressed_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_BEGIN, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleFlashDeflData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD data_len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                     ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD seq = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "FLASH_DEFL_DATA seq=%lu len=%lu", seq, data_len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  seq=%lu len=%lu", seq, data_len);

    if (pkt->size >= 16 + data_len) {
        const BYTE *payload = &pkt->data[16];

        /* Verify checksum */
        BYTE expected = Esptool_CalcChecksum(payload, (int)data_len);
        BYTE received = (BYTE)(pkt->value & 0xFF);
        if (expected != received) {
            TRACE_PROTO(TAG, "FLASH_DEFL_DATA checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            BYTE status_len = ctx->stub_mode ? 2 : 4;
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }

        /* Decompress data */
        if (ctx->flash_uncompressed_size > 0) {
            /* Allocate decompression buffer */
            BYTE *decomp_buf = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ctx->flash_uncompressed_size);
            if (!decomp_buf) {
                TRACE_FW(TAG, "Failed to allocate decompression buffer");
                Serial_PostLog(ctx->hNotify, L"ERR", L"  Failed to allocate decompression buffer");
                BYTE status_len = ctx->stub_mode ? 2 : 4;
                Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
                return;
            }

            /* Initialize decompressor */
            DEFLATE_CTX deflate_ctx;
            deflate_init(&deflate_ctx, payload, data_len, decomp_buf, ctx->flash_uncompressed_size);

            /* Decompress */
            int ret = deflate_decompress(&deflate_ctx);
            if (ret != DEFLATE_OK) {
                TRACE_FW(TAG, "Decompression failed: %d", ret);
                Serial_PostLogF(ctx->hNotify, L"ERR", L"  Decompression failed: %d", ret);
                HeapFree(GetProcessHeap(), 0, decomp_buf);
                BYTE status_len = ctx->stub_mode ? 2 : 4;
                Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
                return;
            }

            /* Write decompressed data to flash */
            DWORD decomp_size = (DWORD)deflate_ctx.out_pos;
            TRACE_PROTO(TAG, "FLASH_DEFL_DATA decompressed %lu -> %lu bytes", data_len, decomp_size);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Decompressed %lu -> %lu bytes", data_len, decomp_size);

            Flash_Write(&ctx->flash, ctx->flash_offset, decomp_buf, decomp_size);
            ctx->flash_offset += decomp_size;
            ctx->flash_seq = seq + 1;

            /* Free decompression buffer */
            HeapFree(GetProcessHeap(), 0, decomp_buf);
        } else {
            /* No decompression needed (uncompressed_size is 0) */
            Flash_Write(&ctx->flash, ctx->flash_offset, payload, data_len);
            ctx->flash_offset += data_len;
            ctx->flash_seq = seq + 1;
        }
    }

    if (ctx->onModified) ctx->onModified();
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_DATA, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleFlashDeflEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "FLASH_DEFL_END");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  End compressed flash download");
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DEFL_END, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleReadFlash(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                 ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD len = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD bsize = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                  ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);

    (void)bsize;

    TRACE_PROTO(TAG, "READ_FLASH addr=0x%08lX len=%lu", addr, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX len=%lu", addr, len);

    if (len > 4094)
        len = 4094;

    BYTE buf[4096];
    /* READ_FLASH is stub-only, 2-byte status prefix */
    buf[0] = 0x00;
    buf[1] = 0x00;
    if (Flash_Read(&ctx->flash, addr, buf + 2, len)) {
        Esptool_SendResponse(ctx, ESP_CMD_READ_FLASH, ctx->last_read_val, ESP_OK, buf, (WORD)(len + 2));
    } else {
        buf[0] = 0x01;
        Esptool_SendResponse(ctx, ESP_CMD_READ_FLASH, ctx->last_read_val, ESP_OK, buf, (WORD)(len + 2));
    }
}

static void HandleEraseFlash(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "ERASE_FLASH");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Erase entire flash");

    Flash_EraseAll(&ctx->flash);
    if (ctx->onModified) ctx->onModified();
    /* ERASE_FLASH is stub-only, always returns 2-byte status */
    Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_FLASH, ctx->last_read_val, ESP_OK, 2, NULL, 2);
}

static void HandleEraseBlock(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD offset = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD len = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "ERASE_BLOCK offset=0x%08lX len=%lu", offset, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  offset=0x%08lX len=%lu", offset, len);

    /* ERASE_REGION is stub-only, always returns 2-byte status */
    if (Flash_Erase(&ctx->flash, offset, len)) {
        if (ctx->onModified) ctx->onModified();
        Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_REGION, ctx->last_read_val, ESP_OK, 2, NULL, 2);
    } else {
        Esptool_SendResponseEx(ctx, ESP_CMD_ERASE_REGION, ctx->last_read_val, ESP_FAIL, 2, NULL, 2);
    }
}

static void HandleFlashMd5(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                 ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD len = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "FLASH_MD5 addr=0x%08lX len=%lu", addr, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX len=%lu", addr, len);

    BYTE md5[16];
    Flash_CalcMd5(&ctx->flash, addr, len, md5);

    /* ROM mode: 2-byte status + 32-byte ASCII hex MD5 = 34 bytes
       Stub mode: 2-byte status + 16-byte binary MD5 = 18 bytes */
    if (ctx->stub_mode) {
        /* Stub mode: return 16-byte binary MD5 */
        BYTE resp[18];
        resp[0] = 0x00;  /* status byte 1 (success) */
        resp[1] = 0x00;  /* status byte 2 (success) */
        memcpy(&resp[2], md5, 16);
        TRACE_PROTO(TAG, "  MD5 (stub, binary)");
        Serial_PostLog(ctx->hNotify, L"ESP", L"  MD5 (stub, binary)");
        Esptool_SendResponse(ctx, ESP_CMD_SPI_FLASH_MD5, ctx->last_read_val, ESP_OK, resp, 18);
    } else {
        /* ROM mode: return 32-byte ASCII hex MD5 */
        char md5str[35];
        md5str[0] = 0x00;
        md5str[1] = 0x00;
        for (int i = 0; i < 16; i++)
            sprintf(&md5str[2 + i * 2], "%02x", md5[i]);
        TRACE_PROTO(TAG, "  MD5=%s", &md5str[2]);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  MD5=%hs", &md5str[2]);
        Esptool_SendResponse(ctx, ESP_CMD_SPI_FLASH_MD5, ctx->last_read_val, ESP_OK, (const BYTE *)md5str, 34);
    }
}

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

    ctx->flash_offset = offset;
    ctx->flash_seq = 0;

    TRACE_PROTO(TAG, "FLASH_BEGIN erase=%lu blocks=%lu bsize=%lu offset=0x%08lX encrypted=%lu",
                erase_size, num_blocks, block_size, offset, encrypted);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  erase=%lu blocks=%lu bsize=%lu offset=0x%08lX encrypted=%lu",
                    erase_size, num_blocks, block_size, offset, encrypted);

    /* Erase the flash region as requested by the host */
    if (erase_size > 0) {
        Flash_Erase(&ctx->flash, offset, erase_size);
        if (ctx->onModified) ctx->onModified();
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Flash erased: offset=0x%08lX size=%lu", offset, erase_size);
    }

    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_BEGIN, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleFlashData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD data_len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                     ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD seq = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    TRACE_PROTO(TAG, "FLASH_DATA seq=%lu len=%lu", seq, data_len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  seq=%lu len=%lu", seq, data_len);

    if (pkt->size >= 16 + data_len) {
        const BYTE *payload = &pkt->data[16];

        /* Verify checksum */
        BYTE expected = Esptool_CalcChecksum(payload, (int)data_len);
        BYTE received = (BYTE)(pkt->value & 0xFF);
        if (expected != received) {
            TRACE_PROTO(TAG, "FLASH_DATA checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Checksum mismatch: expected=0x%02X received=0x%02X", expected, received);
            BYTE status_len = ctx->stub_mode ? 2 : 4;
            Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
            return;
        }

        Flash_Write(&ctx->flash, ctx->flash_offset, payload, data_len);
        ctx->flash_offset += data_len;
        ctx->flash_seq = seq + 1;
    }

    if (ctx->onModified) ctx->onModified();
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_DATA, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleFlashEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD reboot = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "FLASH_END reboot=%lu", reboot);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  reboot=%lu", reboot);
    /* Stub mode: 2-byte status; ROM mode: 4-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_END, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleGetSecurityInfo(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "GET_SECURITY_INFO");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Get security info");

    /* GET_SECURITY_INFO response format:
       2-byte status + security_info (12 bytes)
       Total: 14 bytes
       [0x00, 0x00 (status)] + [0x00,0x00,0x00,0x00 (flags)] + [0x00 (crypt_cnt)] + [0x00 x7 (key_purposes)] */
    BYTE sec_data[14] = {0};
    sec_data[0] = 0x00;  /* status byte 1 (success) */
    sec_data[1] = 0x00;  /* status byte 2 (success) */
    /* Remaining 12 bytes: flags(4) + flash_crypt_cnt(1) + key_purposes(7) = all zeros */
    Esptool_SendResponse(ctx, ESP_CMD_GET_SECURITY_INFO, ctx->last_read_val, ESP_OK, sec_data, 14);
}

static void HandleSpiAttach(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "SPI_ATTACH");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Attach SPI flash");

    /* SPI_ATTACH: ROM mode 4-byte status, stub mode 2-byte status */
    BYTE status_len = ctx->stub_mode ? 2 : 4;
    Esptool_SendResponseEx(ctx, ESP_CMD_SPI_ATTACH, ctx->last_read_val, ESP_OK, status_len, NULL, status_len);
}

static void HandleRunUserCode(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    TRACE_PROTO(TAG, "RUN_USER_CODE");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Run user code (soft reset)");

    /* RUN_USER_CODE: stub-only, fire-and-forget (client does not wait for response) */
    Esptool_SendResponseEx(ctx, ESP_CMD_RUN_USER_CODE, ctx->last_read_val, ESP_OK, 2, NULL, 2);

    /* Reset protocol state for next connection */
    ctx->synced = FALSE;
    ctx->stub_mode = FALSE;
}

BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len)
{
    ESP_PACKET *pkt = &ctx->pkt;

    TRACE_PROTO(TAG, "RX frame len=%d", frame_len);

    if (!ParsePacket(frame, frame_len, pkt)) {
        TRACE_FW(TAG, "Invalid packet");
        return FALSE;
    }

    /* Get command name */
    const char *cmdName = commandTable[pkt->command].name;
    if (!cmdName)
        cmdName = "UNKNOWN";

    /* Get direction string */
    const WCHAR *dirStr = (pkt->direction == ESP_DIR_REQUEST) ? L"REQ" : L"RES";

    /* Log packet summary */
    Serial_PostLogF(ctx->hNotify, L"ESP", L"[%s] %hs size=%u val=0x%08lX",
                    dirStr, cmdName, pkt->size, pkt->value);

    if (pkt->direction != ESP_DIR_REQUEST) {
        TRACE_FW(TAG, "Not a request: 0x%02X", pkt->direction);
        return FALSE;
    }

    switch (pkt->command) {
    case ESP_CMD_SYNC:              HandleSync(ctx, pkt); break;
    case ESP_CMD_READ_REG:          HandleReadReg(ctx, pkt); break;
    case ESP_CMD_WRITE_REG:         HandleWriteReg(ctx, pkt); break;
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
        TRACE_FW(TAG, "Unknown cmd: 0x%02X", pkt->command);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Unknown command: 0x%02X", pkt->command);
        Esptool_SendResponse(ctx, pkt->command, pkt->value, ESP_FAIL, NULL, 4);
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
