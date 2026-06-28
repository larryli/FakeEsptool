/*
 * hal.c - Minimal HAL stubs for unit testing
 *
 * Provides fesp_hal_mem_alloc/free/zero_alloc and fesp_hal_md5_calc
 * so modules that call these functions can link in test context.
 */

#include <stdlib.h>
#include <string.h>

typedef unsigned long DWORD;

void *fesp_hal_mem_alloc(DWORD size) { return malloc(size); }

void *fesp_hal_mem_zero_alloc(DWORD size)
{
    void *p = malloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void fesp_hal_mem_free(void *ptr) { free(ptr); }

/* Deterministic pseudo-hash stub (not a real MD5) */
#define MD5_DIGEST_SIZE 16

void fesp_hal_md5_calc(const unsigned char *data, DWORD len,
                       unsigned char digest[MD5_DIGEST_SIZE])
{
    memset(digest, 0, MD5_DIGEST_SIZE);
    for (DWORD i = 0; i < len; i++) {
        digest[i % MD5_DIGEST_SIZE] ^= data[i];
        digest[(i + 7) % MD5_DIGEST_SIZE] += data[i];
    }
}
