/*
 * fesptool_hal.c - HAL implementation (glue layer)
 *
 * Naming: glue code does not strictly follow fesptool snake_case.
 * Only the interfaces declared in fesptool_hal.h are part of the contract.
 */

#include "fesptool_hal.h"
#include "utils/trace.h"
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
 * GUI-side registration (PascalCase)
 * ======================================================================== */

void FEsptoolSetWriteCallback(ESP_HAL_WRITE_CB cb) { s_writeCb = cb; }
void FEsptoolSetBaudRateCallback(ESP_HAL_BAUDRATE_CB cb) { s_baudRateCb = cb; }
void FEsptoolSetModifiedCallback(ESP_HAL_MODIFIED_CB cb) { s_modifiedCb = cb; }
void FEsptoolSetLogCallback(ESP_HAL_LOGLINE_CB cb, void *ctx)
{
    s_logCb = cb;
    s_logCtx = ctx;
}

/* ========================================================================
 * Engine-side forwarding (snake_case)
 * ======================================================================== */

DWORD fesp_hal_write(const BYTE *data, DWORD len)
{
    if (s_writeCb) {
        return s_writeCb(data, len);
    }
    return 0;
}

BOOL fesp_hal_set_baud_rate(DWORD baudRate)
{
    if (s_baudRateCb) {
        return s_baudRateCb(baudRate);
    }
    return FALSE;
}

void fesp_hal_modified(void)
{
    if (s_modifiedCb) {
        s_modifiedCb();
    }
}

/* ========================================================================
 * Engine-side forwarding (snake_case)
 * ======================================================================== */

static void LogDispatch(const char *tag, bool is_error, const char *fmt,
                        va_list ap)
{
    if (!s_logCb)
        return;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    buf[sizeof(buf) - 1] = '\0';
    s_logCb(tag, buf, is_error, s_logCtx);
}

void fesp_hal_log_i(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef ENABLE_TRACE
    Trace_WriteVa(tag, fmt, ap);
#endif
    LogDispatch(tag, false, fmt, ap);
    va_end(ap);
}

void fesp_hal_log_e(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef ENABLE_TRACE
    Trace_WriteVa(tag, fmt, ap);
#endif
    LogDispatch(tag, true, fmt, ap);
    va_end(ap);
}

/* ========================================================================
 * Engine-side tool functions (forward to utils)
 * ======================================================================== */

#include "utils/deflate.h"
#include "utils/encrypt.h"
#include "utils/md5.h"
#include "utils/mem.h"

void *fesp_hal_mem_alloc(DWORD size) { return Mem_Alloc(size); }
void *fesp_hal_mem_zero_alloc(DWORD size) { return Mem_ZeroAlloc(size); }
void fesp_hal_mem_free(void *ptr) { Mem_Free(ptr); }

void fesp_hal_md5_calc(const BYTE *data, DWORD len, BYTE digest[16])
{
    MD5_Calc(data, len, digest);
}

void fesp_hal_deflate_init(void *ctx, const BYTE *in_buf, size_t in_len,
                           BYTE *out_buf, size_t out_len)
{
    deflate_init(ctx, in_buf, in_len, out_buf, out_len);
}

int fesp_hal_deflate_decompress(void *ctx) { return deflate_decompress(ctx); }

int fesp_hal_encrypt_init(void *ctx, const BYTE *key, int key_len,
                          DWORD flash_addr)
{
    return Encrypt_Init(ctx, key, key_len, flash_addr);
}

int fesp_hal_encrypt_data(void *ctx, const BYTE *in_buf, BYTE *out_buf,
                          DWORD len)
{
    return Encrypt_Data(ctx, in_buf, out_buf, len);
}

int fesp_hal_decrypt_data(void *ctx, const BYTE *in_buf, BYTE *out_buf,
                          DWORD len)
{
    return Decrypt_Data(ctx, in_buf, out_buf, len);
}
