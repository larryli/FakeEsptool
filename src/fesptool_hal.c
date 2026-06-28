/*
 * fesptool_hal.c - HAL implementation (glue layer)
 *
 * Naming: glue code does not strictly follow fesptool snake_case.
 * Only the interfaces declared in fesptool_hal.h are part of the contract.
 */

#include "fesptool_hal.h"
#include "app_commands.h"
#include "serial.h"
#include "utils/trace.h"
#include <stdio.h>

/* ========================================================================
 * State
 * ======================================================================== */

static HWND s_hWnd;
static SERIAL_CTX *s_serial;

/* ========================================================================
 * Initialization (PascalCase)
 * ======================================================================== */

void FEsptoolInit(HWND hWnd, void *serial)
{
    s_hWnd = hWnd;
    s_serial = (SERIAL_CTX *)serial;
}

/* ========================================================================
 * Engine-side forwarding (snake_case)
 * ======================================================================== */

DWORD fesp_hal_write(const BYTE *data, DWORD len)
{
    if (!Serial_IsOpen(s_serial)) {
        return 0;
    }
    return Serial_WriteData(s_serial, data, len, s_hWnd);
}

BOOL fesp_hal_set_baud_rate(DWORD baudRate)
{
    if (!Serial_IsOpen(s_serial)) {
        return FALSE;
    }
    return Serial_SetBaudRate(s_serial, baudRate);
}

void fesp_hal_modified(void) { OnDeviceModified(); }

/* ========================================================================
 * Engine-side logging (snake_case)
 * ======================================================================== */

static void LogDispatch(const char *tag, bool is_error, const char *fmt,
                        va_list ap)
{
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, ap);
    WCHAR wtag[32], wline[1024];
    MultiByteToWideChar(CP_UTF8, 0, tag, -1, wtag, 32);
    MultiByteToWideChar(CP_UTF8, 0, line, -1, wline, 1024);
    Serial_PostLog(s_hWnd, wtag, wline);
}

void fesp_hal_log_i(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef ENABLE_TRACE
    va_list ap2;
    va_copy(ap2, ap);
    Trace_WriteVa(tag, fmt, ap2);
    va_end(ap2);
#endif
    LogDispatch(tag, false, fmt, ap);
    va_end(ap);
}

void fesp_hal_log_e(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef ENABLE_TRACE
    va_list ap2;
    va_copy(ap2, ap);
    Trace_WriteVa(tag, fmt, ap2);
    va_end(ap2);
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

void fesp_hal_deflate_init(fesp_hal_deflate_ctx_t *ctx, const BYTE *in_buf,
                           size_t in_len, BYTE *out_buf, size_t out_len)
{
    Deflate_Init((DEFLATE_CTX *)ctx, in_buf, in_len, out_buf, out_len);
}

int fesp_hal_deflate_decompress(fesp_hal_deflate_ctx_t *ctx)
{
    return Deflate_Decompress((DEFLATE_CTX *)ctx);
}

size_t fesp_hal_deflate_get_output_pos(const fesp_hal_deflate_ctx_t *ctx)
{
    return ((const DEFLATE_CTX *)ctx)->out_pos;
}

int fesp_hal_encrypt_init(fesp_hal_encrypt_ctx_t *ctx, const BYTE *key,
                          int key_len, DWORD flash_addr)
{
    return Encrypt_Init((ENCRYPT_CTX *)ctx, key, key_len, flash_addr);
}

int fesp_hal_encrypt_data(fesp_hal_encrypt_ctx_t *ctx, const BYTE *in_buf,
                          BYTE *out_buf, DWORD len)
{
    return Encrypt_Data((ENCRYPT_CTX *)ctx, in_buf, out_buf, len);
}

int fesp_hal_decrypt_data(fesp_hal_encrypt_ctx_t *ctx, const BYTE *in_buf,
                          BYTE *out_buf, DWORD len)
{
    return Decrypt_Data((ENCRYPT_CTX *)ctx, in_buf, out_buf, len);
}
