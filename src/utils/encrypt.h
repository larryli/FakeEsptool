/*
 * encrypt.h - AES-XTS encryption/decryption for flash encryption
 *
 * Implements the AES-XTS algorithm used by ESP32 flash encryption.
 * All ESP chips use standard AES-XTS with 128-byte blocks (espsecure
 * implementation).
 *
 * Reference: espsecure _flash_encryption_operation_aes_xts
 */

#ifndef ENCRYPT_H
#define ENCRYPT_H

#include <windows.h>

/* Return codes */
#define ENCRYPT_OK 0
#define ENCRYPT_ERROR -1
#define ENCRYPT_BAD_INPUT -2

/* Key lengths */
#define ENCRYPT_KEY_LEN_256 32 /* 256-bit key (XTS-AES-128) */
#define ENCRYPT_KEY_LEN_512 64 /* 512-bit key (XTS-AES-256) */

/* AES-XTS block size (1024 bits = 128 bytes) - espsecure implementation */
#define ENCRYPT_BLOCK_SIZE 128

/*
 * ENCRYPT_CTX - Encryption context
 */
typedef struct {
    BYTE key[64];     /* XTS key (32 or 64 bytes) */
    int key_len;      /* Key length in bytes (32 or 64) */
    DWORD flash_addr; /* Flash address for tweak calculation */
} ENCRYPT_CTX;

/*
 * Encrypt_Init - Initialize encryption context
 *
 * @ctx:        Pointer to encryption context
 * @key:        Key data (32 bytes for XTS-AES-128, 64 bytes for XTS-AES-256)
 * @key_len:    Key length in bytes
 * @flash_addr: Flash address (must be multiple of 16)
 *
 * Returns ENCRYPT_OK on success, ENCRYPT_BAD_INPUT on invalid parameters.
 */
int Encrypt_Init(ENCRYPT_CTX *ctx, const BYTE *key, int key_len,
                 DWORD flash_addr);

/*
 * Encrypt_Data - Encrypt data using AES-XTS
 *
 * @ctx:      Encryption context (initialized with Encrypt_Init)
 * @in_buf:   Input data (plaintext)
 * @out_buf:  Output buffer (ciphertext)
 * @len:      Data length in bytes (must be multiple of 16)
 *
 * Returns ENCRYPT_OK on success, ENCRYPT_BAD_INPUT on invalid parameters.
 */
int Encrypt_Data(ENCRYPT_CTX *ctx, const BYTE *in_buf, BYTE *out_buf,
                 DWORD len);

/*
 * Decrypt_Data - Decrypt data using AES-XTS
 *
 * @ctx:      Encryption context (initialized with Encrypt_Init)
 * @in_buf:   Input data (ciphertext)
 * @out_buf:  Output buffer (plaintext)
 * @len:      Data length in bytes (must be multiple of 16)
 *
 * Returns ENCRYPT_OK on success, ENCRYPT_BAD_INPUT on invalid parameters.
 */
int Decrypt_Data(ENCRYPT_CTX *ctx, const BYTE *in_buf, BYTE *out_buf,
                 DWORD len);

#endif /* ENCRYPT_H */
