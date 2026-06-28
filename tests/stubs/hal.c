/*
 * hal.c - Complete HAL stubs for unit testing
 *
 * Provides all fesp_hal_* functions, deflate, and encrypt stubs
 * so esptool.c and other modules can link in test context.
 */

#include <stdlib.h>
#include <string.h>

typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned char BYTE;

#define TRUE 1
#define FALSE 0

/* ========================================================================
 * Memory
 * ======================================================================== */

void *fesp_hal_mem_alloc(DWORD size) { return malloc(size); }

void *fesp_hal_mem_zero_alloc(DWORD size)
{
    void *p = malloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void fesp_hal_mem_free(void *ptr) { free(ptr); }

/* ========================================================================
 * MD5
 * ======================================================================== */

void fesp_hal_md5_calc(const BYTE *data, DWORD len, BYTE digest[16])
{
    memset(digest, 0, 16);
    for (DWORD i = 0; i < len; i++) {
        digest[i % 16] ^= data[i];
        digest[(i + 7) % 16] += data[i];
    }
}

/* ========================================================================
 * Serial I/O stubs (capture responses for verification)
 * ======================================================================== */

static BYTE s_resp_buf[16384];
static int s_resp_len = 0;
static BOOL s_baud_rate_changed = FALSE;
static DWORD s_last_baud_rate = 0;
static BOOL s_modified_called = FALSE;

DWORD fesp_hal_write(const BYTE *data, DWORD len)
{
    if (s_resp_len + (int)len <= (int)sizeof(s_resp_buf)) {
        memcpy(s_resp_buf + s_resp_len, data, len);
        s_resp_len += (int)len;
    }
    return len;
}

BOOL fesp_hal_set_baud_rate(DWORD baudRate)
{
    s_baud_rate_changed = TRUE;
    s_last_baud_rate = baudRate;
    return TRUE;
}

void fesp_hal_modified(void) { s_modified_called = TRUE; }

/* Test helpers to inspect stub state */
void stub_reset(void)
{
    s_resp_len = 0;
    s_baud_rate_changed = FALSE;
    s_last_baud_rate = 0;
    s_modified_called = FALSE;
}

int stub_get_resp_len(void) { return s_resp_len; }

const BYTE *stub_get_resp(void) { return s_resp_buf; }

BOOL stub_get_baud_changed(void) { return s_baud_rate_changed; }

DWORD stub_get_baud_rate(void) { return s_last_baud_rate; }

BOOL stub_get_modified(void) { return s_modified_called; }

/* ========================================================================
 * DEFLATE stubs (no-op, return success)
 * ======================================================================== */

void deflate_init(void *ctx, const void *in_buf, size_t in_len,
                  void *out_buf, size_t out_len)
{
    (void)ctx;
    (void)in_buf;
    (void)in_len;
    (void)out_buf;
    (void)out_len;
}

int deflate_decompress(void *ctx)
{
    (void)ctx;
    return 0; /* DEFLATE_OK */
}

void fesp_hal_deflate_init(void *ctx, const BYTE *in_buf, size_t in_len,
                           BYTE *out_buf, size_t out_len)
{
    deflate_init(ctx, in_buf, in_len, out_buf, out_len);
}

int fesp_hal_deflate_decompress(void *ctx)
{
    return deflate_decompress(ctx);
}

/* ========================================================================
 * ENCRYPT stubs (passthrough, return success)
 * ======================================================================== */

int Encrypt_Init(void *ctx, const BYTE *key, int key_len, DWORD flash_addr)
{
    (void)ctx;
    (void)key;
    (void)key_len;
    (void)flash_addr;
    return 0; /* ENCRYPT_OK */
}

int Encrypt_Data(void *ctx, const BYTE *in_buf, BYTE *out_buf, DWORD len)
{
    (void)ctx;
    if (in_buf && out_buf && len > 0)
        memcpy(out_buf, in_buf, len);
    return 0;
}

int Decrypt_Data(void *ctx, const BYTE *in_buf, BYTE *out_buf, DWORD len)
{
    (void)ctx;
    if (in_buf && out_buf && len > 0)
        memcpy(out_buf, in_buf, len);
    return 0;
}

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

/* ========================================================================
 * Logging stubs (no-op)
 * ======================================================================== */

#include <stdarg.h>

void fesp_hal_log_i(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}

void fesp_hal_log_e(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}
