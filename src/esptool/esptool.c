/*
 * esptool.c - esptool protocol handler implementation
 *
 * Parses SLIP frames, routes commands, and sends responses.
 */

#include "esptool.h"
#include "../serial.h"
#include "../utils/trace.h"
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
    [0x05] = {"MEM_BEGIN", "Begin memory download"},
    [0x06] = {"MEM_END", "End memory download"},
    [0x07] = {"MEM_DATA", "Memory download data"},
    [0x08] = {"SYNC", "Sync handshake"},
    [0x09] = {"SPI_SET_PARAMS", "Set SPI parameters"},
    [0x0A] = {"READ_REG", "Read register"},
    [0x0B] = {"WRITE_REG", "Write register"},
    [0x0D] = {"SPI_ATTACH", "Attach SPI flash"},
    [0x0F] = {"CHANGE_BAUDRATE", "Change baud rate"},
    [0x13] = {"FLASH_MD5", "Calculate flash MD5"},
    [0x14] = {"FLASH_DATA", "Flash download data"},
    [0x15] = {"READ_FLASH", "Read flash"},
    [0x16] = {"ERASE_FLASH", "Erase flash"},
    [0x17] = {"ERASE_BLOCK", "Erase flash block"},
    [0x20] = {"FLASH_DEFL_BEGIN", "Begin compressed flash download"},
    [0x21] = {"FLASH_DEFL_DATA", "Compressed flash download data"},
    [0x22] = {"FLASH_DEFL_END", "End compressed flash download"},
    [0x23] = {"FLASH_DEFL_MD5", "Calculate compressed flash MD5"},
};

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

void Esptool_SendResponse(ESPTOOL_CTX *ctx, BYTE cmd, DWORD status, const BYTE *data, WORD data_len)
{
    BYTE resp[2048];
    int pos = 0;

    resp[pos++] = ESP_DIR_RESPONSE;
    resp[pos++] = cmd;
    resp[pos++] = (BYTE)(data_len & 0xFF);
    resp[pos++] = (BYTE)(data_len >> 8);
    resp[pos++] = (BYTE)(status & 0xFF);
    resp[pos++] = (BYTE)((status >> 8) & 0xFF);
    resp[pos++] = (BYTE)((status >> 16) & 0xFF);
    resp[pos++] = (BYTE)((status >> 24) & 0xFF);

    if (data && data_len > 0) {
        if (data_len > sizeof(resp) - pos)
            return;
        memcpy(&resp[pos], data, data_len);
    }
    pos += data_len;

    BYTE encoded[4096];
    int enc_len = Slip_Encode(resp, pos, encoded, sizeof(encoded));
    if (enc_len > 0) {
        /* Write to serial port via callback (callback handles UI logging) */
        if (ctx->onWrite) {
            ctx->onWrite(encoded, (DWORD)enc_len);
        }
    }

    TRACE_PROTO(TAG, "TX cmd=0x%02X status=%lu len=%u", cmd, status, data_len);
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
    (void)pkt;
    TRACE_PROTO(TAG, "SYNC received");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Sync handshake");
    ctx->synced = TRUE;
    Esptool_SendResponse(ctx, ESP_CMD_SYNC, ESP_OK, sync_response, ESP_SYNC_SEQ_LEN);
}

static void HandleReadReg(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->value;
    DWORD val = Chip_ReadReg(&ctx->chip, addr);

    TRACE_PROTO(TAG, "READ_REG addr=0x%08lX val=0x%08lX", addr, val);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX -> 0x%08lX", addr, val);
    Esptool_SendResponse(ctx, ESP_CMD_READ_REG, ESP_OK, (const BYTE *)&val, 4);
}

static void HandleWriteReg(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->value;
    DWORD val = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "WRITE_REG addr=0x%08lX val=0x%08lX", addr, val);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX val=0x%08lX", addr, val);
    Chip_WriteReg(&ctx->chip, addr, val);
    if (ctx->onModified) ctx->onModified();
    Esptool_SendResponse(ctx, ESP_CMD_WRITE_REG, ESP_OK, NULL, 0);
}

static void HandleSpiAttach(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    (void)pkt;
    TRACE_PROTO(TAG, "SPI_ATTACH");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Attach SPI flash");

    BYTE resp[4] = {0};
    Esptool_SendResponse(ctx, ESP_CMD_SPI_ATTACH, ESP_OK, resp, 4);
}

static void HandleSpiSetParams(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    if (pkt->size >= 24) {
        DWORD id = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
        DWORD size = pkt->data[16] | ((DWORD)pkt->data[17] << 8) |
                     ((DWORD)pkt->data[18] << 16) | ((DWORD)pkt->data[19] << 24);

        TRACE_PROTO(TAG, "SPI_SET_PARAMS id=0x%08lX size=%lu", id, size);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  id=0x%08lX size=%lu", id, size);

        if (size > 0 && size <= 16 * 1024 * 1024) {
            Flash_Close(&ctx->flash);
            Chip_SetFlashSize(&ctx->chip, size);
            Flash_Init(&ctx->flash, size);
        }
    }

    Esptool_SendResponse(ctx, ESP_CMD_SPI_SET_PARAMS, ESP_OK, NULL, 0);
}

static void HandleChangeBaudrate(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD new_baud = pkt->value;
    DWORD old_baud = 115200;

    if (pkt->size >= 4)
        old_baud = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "CHANGE_BAUDRATE old=%lu new=%lu", old_baud, new_baud);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  old=%lu new=%lu", old_baud, new_baud);

    BYTE resp[8];
    resp[0] = (BYTE)(old_baud & 0xFF);
    resp[1] = (BYTE)((old_baud >> 8) & 0xFF);
    resp[2] = (BYTE)((old_baud >> 16) & 0xFF);
    resp[3] = (BYTE)((old_baud >> 24) & 0xFF);
    resp[4] = (BYTE)(new_baud & 0xFF);
    resp[5] = (BYTE)((new_baud >> 8) & 0xFF);
    resp[6] = (BYTE)((new_baud >> 16) & 0xFF);
    resp[7] = (BYTE)((new_baud >> 24) & 0xFF);

    /* Send response at old baud rate first */
    Esptool_SendResponse(ctx, ESP_CMD_CHANGE_BAUDRATE, ESP_OK, resp, 8);

    /* Then switch to new baud rate */
    if (ctx->onBaudRate) {
        ctx->onBaudRate(new_baud);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Baud rate switched to %lu", new_baud);
    }
}

static void HandleMemBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    (void)ctx;
    DWORD total = pkt->value;
    DWORD blocks = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD bsize = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                  ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD offset = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                   ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);

    TRACE_PROTO(TAG, "MEM_BEGIN total=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                total, blocks, bsize, offset);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  total=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                    total, blocks, bsize, offset);

    Esptool_SendResponse(ctx, ESP_CMD_MEM_BEGIN, ESP_OK, NULL, 0);
}

static void HandleMemData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    (void)ctx;
    DWORD seq = pkt->value;

    TRACE_PROTO(TAG, "MEM_DATA seq=%lu len=%u", seq, pkt->size);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  seq=%lu len=%u", seq, pkt->size);
    Esptool_SendResponse(ctx, ESP_CMD_MEM_DATA, ESP_OK, NULL, 0);
}

static void HandleMemEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    (void)ctx;
    DWORD entry = pkt->value;

    TRACE_PROTO(TAG, "MEM_END entry=0x%08lX", entry);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  entry=0x%08lX", entry);
    Esptool_SendResponse(ctx, ESP_CMD_MEM_END, ESP_OK, NULL, 0);
}

static void HandleFlashDeflBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD total = pkt->value;
    DWORD blocks = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                   ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD bsize = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                  ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);
    DWORD offset = pkt->data[8] | ((DWORD)pkt->data[9] << 8) |
                   ((DWORD)pkt->data[10] << 16) | ((DWORD)pkt->data[11] << 24);

    TRACE_PROTO(TAG, "FLASH_DEFL_BEGIN total=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                total, blocks, bsize, offset);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  total=%lu blocks=%lu bsize=%lu offset=0x%08lX",
                    total, blocks, bsize, offset);

    Esptool_SendResponse(ctx, ESP_CMD_SPI_FLASH_DEFL_BEGIN, ESP_OK, NULL, 0);
}

static void HandleFlashDeflData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD seq = pkt->value;

    TRACE_PROTO(TAG, "FLASH_DEFL_DATA seq=%lu len=%u", seq, pkt->size);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  seq=%lu len=%u", seq, pkt->size);

    if (ctx->onModified) ctx->onModified();
    Esptool_SendResponse(ctx, ESP_CMD_SPI_FLASH_DEFL_DATA, ESP_OK, NULL, 0);
}

static void HandleFlashDeflEnd(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    (void)pkt;
    TRACE_PROTO(TAG, "FLASH_DEFL_END");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  End compressed flash download");
    Esptool_SendResponse(ctx, ESP_CMD_SPI_FLASH_DEFL_END, ESP_OK, NULL, 0);
}

static void HandleFlashDeflMd5(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->value;
    DWORD len = 0;

    if (pkt->size >= 4)
        len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
              ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "FLASH_DEFL_MD5 addr=0x%08lX len=%lu", addr, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX len=%lu", addr, len);

    BYTE md5[16];
    Flash_CalcMd5(&ctx->flash, addr, len, md5);

    char md5str[33];
    for (int i = 0; i < 16; i++)
        sprintf(&md5str[i * 2], "%02x", md5[i]);

    TRACE_PROTO(TAG, "  MD5=%s", md5str);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  MD5=%hs", md5str);
    Esptool_SendResponse(ctx, ESP_CMD_SPI_FLASH_DEFL_MD5, ESP_OK, (const BYTE *)md5str, 32);
}

static void HandleReadFlash(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->value;
    DWORD len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);
    DWORD bsize = pkt->data[4] | ((DWORD)pkt->data[5] << 8) |
                  ((DWORD)pkt->data[6] << 16) | ((DWORD)pkt->data[7] << 24);

    (void)bsize;

    TRACE_PROTO(TAG, "READ_FLASH addr=0x%08lX len=%lu", addr, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX len=%lu", addr, len);

    if (len > 4096)
        len = 4096;

    BYTE buf[4096];
    if (Flash_Read(&ctx->flash, addr, buf, len)) {
        Esptool_SendResponse(ctx, ESP_CMD_SPI_READ_FLASH, ESP_OK, buf, (WORD)len);
    } else {
        Esptool_SendResponse(ctx, ESP_CMD_SPI_READ_FLASH, ESP_FAIL, NULL, 0);
    }
}

static void HandleEraseFlash(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    (void)pkt;
    TRACE_PROTO(TAG, "ERASE_FLASH");
    Serial_PostLog(ctx->hNotify, L"ESP", L"  Erase entire flash");

    Flash_EraseAll(&ctx->flash);
    if (ctx->onModified) ctx->onModified();
    Esptool_SendResponse(ctx, ESP_CMD_SPI_ERASE_FLASH, ESP_OK, NULL, 0);
}

static void HandleEraseBlock(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD offset = pkt->value;
    DWORD len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "ERASE_BLOCK offset=0x%08lX len=%lu", offset, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  offset=0x%08lX len=%lu", offset, len);

    if (Flash_Erase(&ctx->flash, offset, len)) {
        if (ctx->onModified) ctx->onModified();
        Esptool_SendResponse(ctx, ESP_CMD_SPI_ERASE_BLOCK, ESP_OK, NULL, 0);
    } else {
        Esptool_SendResponse(ctx, ESP_CMD_SPI_ERASE_BLOCK, ESP_FAIL, NULL, 0);
    }
}

static void HandleFlashMd5(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    DWORD addr = pkt->value;
    DWORD len = pkt->data[0] | ((DWORD)pkt->data[1] << 8) |
                ((DWORD)pkt->data[2] << 16) | ((DWORD)pkt->data[3] << 24);

    TRACE_PROTO(TAG, "FLASH_MD5 addr=0x%08lX len=%lu", addr, len);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  addr=0x%08lX len=%lu", addr, len);

    BYTE md5[16];
    Flash_CalcMd5(&ctx->flash, addr, len, md5);

    char md5str[33];
    for (int i = 0; i < 16; i++)
        sprintf(&md5str[i * 2], "%02x", md5[i]);

    TRACE_PROTO(TAG, "  MD5=%s", md5str);
    Serial_PostLogF(ctx->hNotify, L"ESP", L"  MD5=%hs", md5str);
    Esptool_SendResponse(ctx, ESP_CMD_SPI_FLASH_MD5, ESP_OK, (const BYTE *)md5str, 32);
}

BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len)
{
    ESP_PACKET pkt;

    TRACE_PROTO(TAG, "RX frame len=%d", frame_len);

    if (!ParsePacket(frame, frame_len, &pkt)) {
        TRACE_FW(TAG, "Invalid packet");
        return FALSE;
    }

    /* Get command name */
    const char *cmdName = commandTable[pkt.command].name;
    if (!cmdName)
        cmdName = "UNKNOWN";

    /* Get direction string */
    const WCHAR *dirStr = (pkt.direction == ESP_DIR_REQUEST) ? L"REQ" : L"RES";

    /* Log packet summary */
    Serial_PostLogF(ctx->hNotify, L"ESP", L"[%s] %hs size=%u val=0x%08lX",
                    dirStr, cmdName, pkt.size, pkt.value);

    if (pkt.direction != ESP_DIR_REQUEST) {
        TRACE_FW(TAG, "Not a request: 0x%02X", pkt.direction);
        return FALSE;
    }

    switch (pkt.command) {
    case ESP_CMD_SYNC:                 HandleSync(ctx, &pkt); break;
    case ESP_CMD_READ_REG:             HandleReadReg(ctx, &pkt); break;
    case ESP_CMD_WRITE_REG:            HandleWriteReg(ctx, &pkt); break;
    case ESP_CMD_SPI_ATTACH:           HandleSpiAttach(ctx, &pkt); break;
    case ESP_CMD_SPI_SET_PARAMS:       HandleSpiSetParams(ctx, &pkt); break;
    case ESP_CMD_CHANGE_BAUDRATE:      HandleChangeBaudrate(ctx, &pkt); break;
    case ESP_CMD_MEM_BEGIN:            HandleMemBegin(ctx, &pkt); break;
    case ESP_CMD_MEM_DATA:             HandleMemData(ctx, &pkt); break;
    case ESP_CMD_MEM_END:              HandleMemEnd(ctx, &pkt); break;
    case ESP_CMD_SPI_FLASH_DEFL_BEGIN: HandleFlashDeflBegin(ctx, &pkt); break;
    case ESP_CMD_SPI_FLASH_DEFL_DATA:  HandleFlashDeflData(ctx, &pkt); break;
    case ESP_CMD_SPI_FLASH_DEFL_END:   HandleFlashDeflEnd(ctx, &pkt); break;
    case ESP_CMD_SPI_FLASH_DEFL_MD5:   HandleFlashDeflMd5(ctx, &pkt); break;
    case ESP_CMD_SPI_READ_FLASH:       HandleReadFlash(ctx, &pkt); break;
    case ESP_CMD_SPI_ERASE_FLASH:      HandleEraseFlash(ctx, &pkt); break;
    case ESP_CMD_SPI_ERASE_BLOCK:      HandleEraseBlock(ctx, &pkt); break;
    case ESP_CMD_SPI_FLASH_MD5:        HandleFlashMd5(ctx, &pkt); break;
    default:
        TRACE_FW(TAG, "Unknown cmd: 0x%02X", pkt.command);
        Serial_PostLogF(ctx->hNotify, L"ESP", L"  Unknown command: 0x%02X", pkt.command);
        Esptool_SendResponse(ctx, pkt.command, ESP_FAIL, NULL, 0);
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
