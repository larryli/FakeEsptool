/*
 * md5.c - MD5 hash implementation (Windows CryptoAPI)
 *
 * Wraps Windows CryptoAPI for MD5 calculation.
 * For porting to other platforms, replace this file with an alternative
 * MD5 implementation (e.g., OpenSSL, mbedTLS, or standalone RFC 1321).
 */

#include "md5.h"
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

void MD5_Calc(const BYTE *data, DWORD len, BYTE digest[MD5_DIGEST_SIZE])
{
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    memset(digest, 0, MD5_DIGEST_SIZE);

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT))
        return;

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return;
    }

    CryptHashData(hHash, data, len, 0);

    DWORD hashLen = MD5_DIGEST_SIZE;
    CryptGetHashParam(hHash, HP_HASHVAL, digest, &hashLen, 0);

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}
