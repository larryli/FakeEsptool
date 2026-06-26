/*
 * md5.h - MD5 hash decoupling layer
 *
 * Wraps platform-specific MD5 implementation behind a common interface.
 * flash.c calls this instead of CryptoAPI directly.
 * For porting: replace md5.c implementation, keep this header unchanged.
 */

#ifndef UTILS_MD5_H
#define UTILS_MD5_H

#include <windows.h>

#define MD5_DIGEST_SIZE 16

/*
 * MD5_Calc - Calculate MD5 hash
 *
 * @data:   Input data
 * @len:    Data length in bytes
 * @digest: Output 16-byte MD5 digest
 */
void MD5_Calc(const BYTE *data, DWORD len, BYTE digest[MD5_DIGEST_SIZE]);

#endif /* UTILS_MD5_H */
