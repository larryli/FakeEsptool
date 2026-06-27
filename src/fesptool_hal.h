/*
 * fesptool_hal.h - Hardware Abstraction Layer for fesptool engine
 *
 * Platform contract: defines ALL external interfaces the simulation
 * engine needs. Implementer must provide callbacks and tool functions.
 *
 * Naming convention:
 *   fesp_hal_xxx      - engine-side tool functions (snake_case)
 *   FESPTOOL_HAL_xxx  - GUI-side log macros
 *   FEsptoolXxx       - GUI-side registration functions (PascalCase)
 */

#ifndef FESPTOOL_HAL_H
#define FESPTOOL_HAL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#include "serial.h"

/* ========================================================================
 * Engine-side tool functions (snake_case)
 * ======================================================================== */

/* Serial write */
DWORD fesp_hal_write(const BYTE *data, DWORD len);

/* Baud rate change */
BOOL fesp_hal_set_baud_rate(DWORD baudRate);

/* Device modification notification */
void fesp_hal_modified(void);

/* Memory */
void *fesp_hal_mem_alloc(DWORD size);
void *fesp_hal_mem_zero_alloc(DWORD size);
void fesp_hal_mem_free(void *ptr);

/* MD5 */
void fesp_hal_md5_calc(const BYTE *data, DWORD len, BYTE digest[16]);

/* DEFLATE decompression */
void fesp_hal_deflate_init(void *ctx, const BYTE *in_buf, size_t in_len,
                           BYTE *out_buf, size_t out_len);
int fesp_hal_deflate_decompress(void *ctx);

/* AES-XTS encrypt/decrypt */
int fesp_hal_encrypt_init(void *ctx, const BYTE *key, int key_len,
                          DWORD flash_addr);
int fesp_hal_encrypt_data(void *ctx, const BYTE *in_buf, BYTE *out_buf,
                          DWORD len);
int fesp_hal_decrypt_data(void *ctx, const BYTE *in_buf, BYTE *out_buf,
                          DWORD len);

/* ========================================================================
 * GUI-side log macros (FESPTOOL_HAL_xxx)
 * ======================================================================== */

void fesp_hal_log_i(const char *tag, const char *fmt, ...);
void fesp_hal_log_e(const char *tag, const char *fmt, ...);

#define FESP_HAL_LOGI(tag, ...) fesp_hal_log_i(tag, __VA_ARGS__)
#define FESP_HAL_LOGE(tag, ...) fesp_hal_log_e(tag, __VA_ARGS__)

/* Debug log: compile-time controlled, no callback needed */
#ifdef ENABLE_TRACE
void Trace_Write(const char *tag, const char *fmt, ...);
#define FESP_HAL_LOGD(TAG, ...) Trace_Write(TAG, __VA_ARGS__)
#define FESP_HAL_LOG_HAS_DEBUG 1
#else
#define FESP_HAL_LOGD(TAG, ...) ((void)0)
#define FESP_HAL_LOG_HAS_DEBUG 0
#endif

/* ========================================================================
 * Initialization (PascalCase)
 * ======================================================================== */

void FEsptoolInit(HWND hWnd, SERIAL_CTX *serial);

#endif /* FESPTOOL_HAL_H */
