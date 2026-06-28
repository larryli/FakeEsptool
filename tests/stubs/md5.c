/*
 * md5.c - Deterministic stub for unit testing
 *
 * Produces a non-crypto hash sufficient to verify flash_calc_md5
 * correctly slices and passes data.
 */

#include <string.h>

#define MD5_DIGEST_SIZE 16

void MD5_Calc(const unsigned char *data, unsigned long len,
              unsigned char digest[MD5_DIGEST_SIZE])
{
    memset(digest, 0, MD5_DIGEST_SIZE);

    for (unsigned long i = 0; i < len; i++) {
        digest[i % MD5_DIGEST_SIZE] ^= data[i];
        digest[(i + 7) % MD5_DIGEST_SIZE] += data[i];
    }
}
