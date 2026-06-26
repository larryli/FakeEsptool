/*
 * esptool_hal.h - Hardware Abstraction Layer for esptool engine
 *
 * Platform contract: defines ALL external interfaces the esptool
 * simulation engine needs.使用者必须实现回调和工具函数。
 *
 * Output callbacks (engine → external):
 *   Write / SetBaudRate / Modified / LogI / LogE
 *
 * Tool functions (engine ← platform implementation):
 *   Mem_* / MD5_Calc / deflate_* / Encrypt_* / Decrypt_*
 *
 * Debug log macro (compile-time controlled):
 *   EsptoolHal_LogD
 */

#ifndef ESPTOOL_HAL_H
#define ESPTOOL_HAL_H

#include <windows.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * Output callbacks (engine → external,使用者必须实现)
 * ======================================================================== */

/* 写数据到串口 */
typedef DWORD (*ESP_HAL_WRITE_CB)(const BYTE *data, DWORD len);
void EsptoolHal_SetWriteCallback(ESP_HAL_WRITE_CB cb);
DWORD EsptoolHal_Write(const BYTE *data, DWORD len);

/* 波特率切换 */
typedef BOOL (*ESP_HAL_BAUDRATE_CB)(DWORD baudRate);
void EsptoolHal_SetBaudRateCallback(ESP_HAL_BAUDRATE_CB cb);
BOOL EsptoolHal_SetBaudRate(DWORD baudRate);

/* 设备修改通知 */
typedef void (*ESP_HAL_MODIFIED_CB)(void);
void EsptoolHal_SetModifiedCallback(ESP_HAL_MODIFIED_CB cb);
void EsptoolHal_Modified(void);

/* 日志 — Info / Error，使用者决定去向（GUI/终端） */
typedef void (*ESP_HAL_LOGLINE_CB)(const char *tag, const char *line,
                                   bool is_error, void *ctx);
void EsptoolHal_SetLogCallback(ESP_HAL_LOGLINE_CB cb, void *ctx);
void EsptoolHal_LogI(const char *tag, const char *fmt, ...);
void EsptoolHal_LogE(const char *tag, const char *fmt, ...);

/* ========================================================================
 * Tool functions (engine ← platform implementation,使用者必须实现)
 * ======================================================================== */

/* 内存管理 */
void *EsptoolHal_MemAlloc(DWORD size);
void *EsptoolHal_MemZeroAlloc(DWORD size);
void  EsptoolHal_MemFree(void *ptr);

/* MD5 */
void EsptoolHal_MD5Calc(const BYTE *data, DWORD len, BYTE digest[16]);

/* DEFLATE 解压 */
void EsptoolHal_DeflateInit(void *ctx, const BYTE *in_buf, size_t in_len,
                            BYTE *out_buf, size_t out_len);
int  EsptoolHal_DeflateDecompress(void *ctx);

/* AES-XTS 加解密 */
int EsptoolHal_EncryptInit(void *ctx, const BYTE *key,
                           int key_len, DWORD flash_addr);
int EsptoolHal_EncryptData(void *ctx, const BYTE *in_buf,
                           BYTE *out_buf, DWORD len);
int EsptoolHal_DecryptData(void *ctx, const BYTE *in_buf,
                           BYTE *out_buf, DWORD len);

/* ========================================================================
 * Debug 日志宏（编译期可控，不走回调）
 * ======================================================================== */

#ifdef ENABLE_TRACE
void Trace_Write(const char *tag, const char *fmt, ...);
#define EsptoolHal_LogD(TAG, ...) Trace_Write(TAG, __VA_ARGS__)
#else
#define EsptoolHal_LogD(TAG, ...) ((void)0)
#endif

#endif /* ESPTOOL_HAL_H */
