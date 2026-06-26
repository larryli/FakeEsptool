/*
 * esptool_hal.c - HAL callback storage and forwarding
 *
 * Provides callback registration and forwarding functions for the
 * esptool simulation engine. LogI/LogE use a single callback with
 * is_error flag to distinguish levels.
 */

#include "esptool_hal.h"
#include <stdio.h>

/* ========================================================================
 * Callback storage
 * ======================================================================== */

static ESP_HAL_WRITE_CB s_writeCb = NULL;
static ESP_HAL_BAUDRATE_CB s_baudRateCb = NULL;
static ESP_HAL_MODIFIED_CB s_modifiedCb = NULL;
static ESP_HAL_LOGLINE_CB s_logCb = NULL;
static void *s_logCtx = NULL;

/* ========================================================================
 * Write
 * ======================================================================== */

void EsptoolHal_SetWriteCallback(ESP_HAL_WRITE_CB cb)
{
    s_writeCb = cb;
}

DWORD EsptoolHal_Write(const BYTE *data, DWORD len)
{
    if (s_writeCb)
        return s_writeCb(data, len);
    return 0;
}

/* ========================================================================
 * Baud rate
 * ======================================================================== */

void EsptoolHal_SetBaudRateCallback(ESP_HAL_BAUDRATE_CB cb)
{
    s_baudRateCb = cb;
}

BOOL EsptoolHal_SetBaudRate(DWORD baudRate)
{
    if (s_baudRateCb)
        return s_baudRateCb(baudRate);
    return FALSE;
}

/* ========================================================================
 * Modified
 * ======================================================================== */

void EsptoolHal_SetModifiedCallback(ESP_HAL_MODIFIED_CB cb)
{
    s_modifiedCb = cb;
}

void EsptoolHal_Modified(void)
{
    if (s_modifiedCb)
        s_modifiedCb();
}

/* ========================================================================
 * Log (Info + Error share one callback, distinguished by is_error)
 * ======================================================================== */

void EsptoolHal_SetLogCallback(ESP_HAL_LOGLINE_CB cb, void *ctx)
{
    s_logCb = cb;
    s_logCtx = ctx;
}

static void LogDispatch(const char *tag, bool is_error, const char *fmt,
                        va_list ap)
{
    if (!s_logCb)
        return;
    char buf[1024];
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    buf[sizeof(buf) - 1] = '\0';
    s_logCb(tag, buf, is_error, s_logCtx);
}

void EsptoolHal_LogI(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogDispatch(tag, false, fmt, ap);
    va_end(ap);
}

void EsptoolHal_LogE(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogDispatch(tag, true, fmt, ap);
    va_end(ap);
}

/* ========================================================================
 * Tool functions — forward to utils implementations
 * ======================================================================== */

#include "utils/mem.h"
#include "utils/md5.h"
#include "utils/deflate.h"
#include "utils/encrypt.h"

void *EsptoolHal_MemAlloc(DWORD size)
{
    return Mem_Alloc(size);
}

void *EsptoolHal_MemZeroAlloc(DWORD size)
{
    return Mem_ZeroAlloc(size);
}

void EsptoolHal_MemFree(void *ptr)
{
    Mem_Free(ptr);
}

void EsptoolHal_MD5Calc(const BYTE *data, DWORD len, BYTE digest[16])
{
    MD5_Calc(data, len, digest);
}

void EsptoolHal_DeflateInit(void *ctx, const BYTE *in_buf, size_t in_len,
                            BYTE *out_buf, size_t out_len)
{
    deflate_init(ctx, in_buf, in_len, out_buf, out_len);
}

int EsptoolHal_DeflateDecompress(void *ctx)
{
    return deflate_decompress(ctx);
}

int EsptoolHal_EncryptInit(void *ctx, const BYTE *key, int key_len,
                           DWORD flash_addr)
{
    return Encrypt_Init(ctx, key, key_len, flash_addr);
}

int EsptoolHal_EncryptData(void *ctx, const BYTE *in_buf, BYTE *out_buf,
                           DWORD len)
{
    return Encrypt_Data(ctx, in_buf, out_buf, len);
}

int EsptoolHal_DecryptData(void *ctx, const BYTE *in_buf, BYTE *out_buf,
                           DWORD len)
{
    return Decrypt_Data(ctx, in_buf, out_buf, len);
}
