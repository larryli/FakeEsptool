/*
 * encrypt.c - AES-XTS encryption/decryption implementation
 *
 * Implements the AES-XTS algorithm used by ESP32 flash encryption.
 * All ESP chips use standard AES-XTS with 128-byte blocks (espsecure
 * implementation).
 *
 * Reference: espsecure _flash_encryption_operation_aes_xts
 * AES-XTS specification: IEEE 1619-2007
 */

#include "encrypt.h"
#include <string.h>

/* AES S-Box */
static const BYTE sbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16};

/* AES Inverse S-Box */
static const BYTE inv_sbox[256] = {
    0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E,
    0x81, 0xF3, 0xD7, 0xFB, 0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87,
    0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB, 0x54, 0x7B, 0x94, 0x32,
    0xA6, 0xC2, 0x23, 0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
    0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2, 0x76, 0x5B, 0xA2, 0x49,
    0x6D, 0x8B, 0xD1, 0x25, 0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92, 0x6C, 0x70, 0x48, 0x50,
    0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
    0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A, 0xF7, 0xE4, 0x58, 0x05,
    0xB8, 0xB3, 0x45, 0x06, 0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02,
    0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B, 0x3A, 0x91, 0x11, 0x41,
    0x4F, 0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
    0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85, 0xE2, 0xF9, 0x37, 0xE8,
    0x1C, 0x75, 0xDF, 0x6E, 0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89,
    0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B, 0xFC, 0x56, 0x3E, 0x4B,
    0xC6, 0xD2, 0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
    0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59,
    0x27, 0x80, 0xEC, 0x5F, 0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D,
    0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF, 0xA0, 0xE0, 0x3B, 0x4D,
    0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26, 0xE1, 0x69, 0x14, 0x63,
    0x55, 0x21, 0x0C, 0x7D};

/* AES Round Constants */
static const BYTE rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                              0x20, 0x40, 0x80, 0x1B, 0x36};

/* AES context structure */
typedef struct {
    BYTE round_key[240]; /* Max size for AES-256 (15 rounds * 16 bytes) */
    int nr;              /* Number of rounds (10 for AES-128, 14 for AES-256) */
} AES_CTX;

/*
 * AES key expansion for AES-128 (16-byte key)
 */
static void aes_key_expand_128(AES_CTX *ctx, const BYTE key[16])
{
    BYTE words[44][4];
    int i, j;

    ctx->nr = 10;

    /* Copy key to first 4 words */
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            words[i][j] = key[i * 4 + j];
        }
    }

    /* Generate remaining words */
    for (i = 4; i < 44; i++) {
        BYTE temp[4];
        memcpy(temp, words[i - 1], 4);

        if (i % 4 == 0) {
            /* RotWord */
            BYTE t = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = t;

            /* SubWord */
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];

            /* XOR with Rcon */
            temp[0] ^= rcon[i / 4 - 1];
        }

        for (j = 0; j < 4; j++) {
            words[i][j] = words[i - 4][j] ^ temp[j];
        }
    }

    /* Convert words to round keys */
    for (i = 0; i < 11; i++) {
        for (j = 0; j < 16; j++) {
            ctx->round_key[i * 16 + j] = words[i * 4 + j / 4][j % 4];
        }
    }
}

/*
 * AES key expansion for AES-256 (32-byte key)
 */
static void aes_key_expand_256(AES_CTX *ctx, const BYTE key[32])
{
    BYTE words[60][4];
    int i, j;

    ctx->nr = 14;

    /* Copy key to first 8 words */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 4; j++) {
            words[i][j] = key[i * 4 + j];
        }
    }

    /* Generate remaining words */
    for (i = 8; i < 60; i++) {
        BYTE temp[4];
        memcpy(temp, words[i - 1], 4);

        if (i % 8 == 0) {
            /* RotWord */
            BYTE t = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = t;

            /* SubWord */
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];

            /* XOR with Rcon */
            temp[0] ^= rcon[i / 8 - 1];
        } else if (i % 8 == 4) {
            /* SubWord for AES-256 */
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];
        }

        for (j = 0; j < 4; j++) {
            words[i][j] = words[i - 8][j] ^ temp[j];
        }
    }

    /* Convert words to round keys */
    for (i = 0; i < 15; i++) {
        for (j = 0; j < 16; j++) {
            ctx->round_key[i * 16 + j] = words[i * 4 + j / 4][j % 4];
        }
    }
}

/*
 * GF(2^8) multiplication
 */
static BYTE gmul(BYTE a, BYTE b)
{
    BYTE p = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (b & 1) {
            p ^= a;
        }
        BYTE hi = a & 0x80;
        a <<= 1;
        if (hi) {
            a ^= 0x1B;
        }
        b >>= 1;
    }
    return p;
}

/*
 * AES-ECB encrypt one block (16 bytes)
 *
 * State layout: column-major order
 * state[row][col] = data[row + col*4]
 */
static void aes_ecb_encrypt(AES_CTX *ctx, const BYTE in[16], BYTE out[16])
{
    BYTE state[4][4];
    int round, row, col;

    /* Convert input to state (column-major) */
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            state[row][col] = in[row + col * 4];
        }
    }

    /* Round 0: AddRoundKey */
    for (col = 0; col < 4; col++) {
        for (row = 0; row < 4; row++) {
            state[row][col] ^= ctx->round_key[row + col * 4];
        }
    }

    /* Rounds 1 to nr-1 */
    for (round = 1; round < ctx->nr; round++) {
        /* SubBytes */
        for (row = 0; row < 4; row++) {
            for (col = 0; col < 4; col++) {
                state[row][col] = sbox[state[row][col]];
            }
        }

        /* ShiftRows */
        BYTE temp;
        /* Row 1: shift left by 1 */
        temp = state[1][0];
        state[1][0] = state[1][1];
        state[1][1] = state[1][2];
        state[1][2] = state[1][3];
        state[1][3] = temp;
        /* Row 2: shift left by 2 */
        temp = state[2][0];
        state[2][0] = state[2][2];
        state[2][2] = temp;
        temp = state[2][1];
        state[2][1] = state[2][3];
        state[2][3] = temp;
        /* Row 3: shift left by 3 (or right by 1) */
        temp = state[3][3];
        state[3][3] = state[3][2];
        state[3][2] = state[3][1];
        state[3][1] = state[3][0];
        state[3][0] = temp;

        /* MixColumns */
        for (col = 0; col < 4; col++) {
            BYTE s0 = state[0][col];
            BYTE s1 = state[1][col];
            BYTE s2 = state[2][col];
            BYTE s3 = state[3][col];
            state[0][col] = gmul(2, s0) ^ gmul(3, s1) ^ s2 ^ s3;
            state[1][col] = s0 ^ gmul(2, s1) ^ gmul(3, s2) ^ s3;
            state[2][col] = s0 ^ s1 ^ gmul(2, s2) ^ gmul(3, s3);
            state[3][col] = gmul(3, s0) ^ s1 ^ s2 ^ gmul(2, s3);
        }

        /* AddRoundKey */
        for (col = 0; col < 4; col++) {
            for (row = 0; row < 4; row++) {
                state[row][col] ^= ctx->round_key[round * 16 + row + col * 4];
            }
        }
    }

    /* Final round: SubBytes, ShiftRows, AddRoundKey (no MixColumns) */
    /* SubBytes */
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            state[row][col] = sbox[state[row][col]];
        }
    }

    /* ShiftRows */
    BYTE temp;
    temp = state[1][0];
    state[1][0] = state[1][1];
    state[1][1] = state[1][2];
    state[1][2] = state[1][3];
    state[1][3] = temp;
    temp = state[2][0];
    state[2][0] = state[2][2];
    state[2][2] = temp;
    temp = state[2][1];
    state[2][1] = state[2][3];
    state[2][3] = temp;
    temp = state[3][3];
    state[3][3] = state[3][2];
    state[3][2] = state[3][1];
    state[3][1] = state[3][0];
    state[3][0] = temp;

    /* AddRoundKey */
    for (col = 0; col < 4; col++) {
        for (row = 0; row < 4; row++) {
            state[row][col] ^= ctx->round_key[ctx->nr * 16 + row + col * 4];
        }
    }

    /* Convert state to output (column-major) */
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            out[row + col * 4] = state[row][col];
        }
    }
}

/*
 * AES-ECB decrypt one block (16 bytes)
 */
static void aes_ecb_decrypt(AES_CTX *ctx, const BYTE in[16], BYTE out[16])
{
    BYTE state[4][4];
    int round, row, col;

    /* Convert input to state (column-major) */
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            state[row][col] = in[row + col * 4];
        }
    }

    /* AddRoundKey (final round) */
    for (col = 0; col < 4; col++) {
        for (row = 0; row < 4; row++) {
            state[row][col] ^= ctx->round_key[ctx->nr * 16 + row + col * 4];
        }
    }

    /* InvShiftRows */
    BYTE temp;
    temp = state[1][3];
    state[1][3] = state[1][2];
    state[1][2] = state[1][1];
    state[1][1] = state[1][0];
    state[1][0] = temp;
    temp = state[2][0];
    state[2][0] = state[2][2];
    state[2][2] = temp;
    temp = state[2][1];
    state[2][1] = state[2][3];
    state[2][3] = temp;
    temp = state[3][0];
    state[3][0] = state[3][1];
    state[3][1] = state[3][2];
    state[3][2] = state[3][3];
    state[3][3] = temp;

    /* InvSubBytes */
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            state[row][col] = inv_sbox[state[row][col]];
        }
    }

    /* Rounds nr-1 to 1 */
    for (round = ctx->nr - 1; round > 0; round--) {
        /* AddRoundKey */
        for (col = 0; col < 4; col++) {
            for (row = 0; row < 4; row++) {
                state[row][col] ^= ctx->round_key[round * 16 + row + col * 4];
            }
        }

        /* InvMixColumns */
        for (col = 0; col < 4; col++) {
            BYTE s0 = state[0][col];
            BYTE s1 = state[1][col];
            BYTE s2 = state[2][col];
            BYTE s3 = state[3][col];
            state[0][col] =
                gmul(14, s0) ^ gmul(11, s1) ^ gmul(13, s2) ^ gmul(9, s3);
            state[1][col] =
                gmul(9, s0) ^ gmul(14, s1) ^ gmul(11, s2) ^ gmul(13, s3);
            state[2][col] =
                gmul(13, s0) ^ gmul(9, s1) ^ gmul(14, s2) ^ gmul(11, s3);
            state[3][col] =
                gmul(11, s0) ^ gmul(13, s1) ^ gmul(9, s2) ^ gmul(14, s3);
        }

        /* InvShiftRows */
        BYTE temp;
        temp = state[1][3];
        state[1][3] = state[1][2];
        state[1][2] = state[1][1];
        state[1][1] = state[1][0];
        state[1][0] = temp;
        temp = state[2][0];
        state[2][0] = state[2][2];
        state[2][2] = temp;
        temp = state[2][1];
        state[2][1] = state[2][3];
        state[2][3] = temp;
        temp = state[3][0];
        state[3][0] = state[3][1];
        state[3][1] = state[3][2];
        state[3][2] = state[3][3];
        state[3][3] = temp;

        /* InvSubBytes */
        for (row = 0; row < 4; row++) {
            for (col = 0; col < 4; col++) {
                state[row][col] = inv_sbox[state[row][col]];
            }
        }
    }

    /* AddRoundKey (round 0) */
    for (col = 0; col < 4; col++) {
        for (row = 0; row < 4; row++) {
            state[row][col] ^= ctx->round_key[row + col * 4];
        }
    }

    /* Convert state to output (column-major) */
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            out[row + col * 4] = state[row][col];
        }
    }
}

/*
 * GF(2^128) multiplication for XTS tweak
 *
 * Multiplies a 128-bit value by alpha (x) in GF(2^128)
 * with modulus x^128 + x^7 + x^2 + x + 1
 */
static void gf128_mul_alpha(BYTE block[16])
{
    int i;
    BYTE carry = 0;

    for (i = 0; i < 16; i++) {
        BYTE new_carry = (block[i] & 0x80) ? 1 : 0;
        block[i] = (block[i] << 1) | carry;
        carry = new_carry;
    }

    if (carry) {
        block[0] ^= 0x87; /* x^128 + x^7 + x^2 + x + 1 */
    }
}

/*
 * Reverse byte order (for espsecure AES-XTS)
 */
static void reverse_bytes(BYTE *buf, int len)
{
    int i;
    for (i = 0; i < len / 2; i++) {
        BYTE temp = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = temp;
    }
}

/*
 * Encrypt_Init - Initialize encryption context
 */
int Encrypt_Init(ENCRYPT_CTX *ctx, const BYTE *key, int key_len,
                 DWORD flash_addr)
{
    if (!ctx || !key) {
        return ENCRYPT_BAD_INPUT;
    }

    if (key_len != 32 && key_len != 64) {
        return ENCRYPT_BAD_INPUT;
    }

    if (flash_addr % 16 != 0) {
        return ENCRYPT_BAD_INPUT;
    }

    memset(ctx, 0, sizeof(ENCRYPT_CTX));
    memcpy(ctx->key, key, key_len);
    ctx->key_len = key_len;
    ctx->flash_addr = flash_addr;

    return ENCRYPT_OK;
}

/*
 * Encrypt_Data - Encrypt data using AES-XTS
 *
 * Reference: espsecure _flash_encryption_operation_aes_xts
 */
int Encrypt_Data(ENCRYPT_CTX *ctx, const BYTE *in_buf, BYTE *out_buf, DWORD len)
{
    AES_CTX aes_data;
    AES_CTX aes_tweak;
    BYTE tweak[16];
    DWORD offset;

    if (!ctx || !in_buf || !out_buf) {
        return ENCRYPT_BAD_INPUT;
    }

    if (len % 16 != 0) {
        return ENCRYPT_BAD_INPUT;
    }

    if (len == 0) {
        return ENCRYPT_BAD_INPUT;
    }

    /* Initialize AES with key (AES-128 for 256-bit key, AES-256 for 512-bit
     * key) */
    if (ctx->key_len == 32) {
        aes_key_expand_128(&aes_data, ctx->key);
        aes_key_expand_128(&aes_tweak, ctx->key + 16);
    } else {
        aes_key_expand_256(&aes_data, ctx->key);
        aes_key_expand_256(&aes_tweak, ctx->key + 32);
    }

    /* Process each 128-byte block (espsecure implementation) */
    for (offset = 0; offset < len; offset += ENCRYPT_BLOCK_SIZE) {
        DWORD block_len = len - offset;
        if (block_len > ENCRYPT_BLOCK_SIZE) {
            block_len = ENCRYPT_BLOCK_SIZE;
        }

        /* Calculate tweak (little-endian flash address, padded to 16 bytes)
         * Reference: espsecure struct.pack("<I", (flash_address & ~0x7F)) +
         * (b"\x00" * 12)
         */
        DWORD tweak_addr = (ctx->flash_addr + offset) & ~0x7F;
        tweak[0] = (BYTE)(tweak_addr & 0xFF);
        tweak[1] = (BYTE)((tweak_addr >> 8) & 0xFF);
        tweak[2] = (BYTE)((tweak_addr >> 16) & 0xFF);
        tweak[3] = (BYTE)((tweak_addr >> 24) & 0xFF);
        memset(tweak + 4, 0, 12);

        /* Copy input block */
        BYTE block[ENCRYPT_BLOCK_SIZE];
        memcpy(block, in_buf + offset, block_len);
        if (block_len < ENCRYPT_BLOCK_SIZE) {
            memset(block + block_len, 0, ENCRYPT_BLOCK_SIZE - block_len);
        }

        /* Reverse input bytes (espsecure does this) */
        reverse_bytes(block, ENCRYPT_BLOCK_SIZE);

        /* Encrypt tweak */
        BYTE enc_tweak[16];
        aes_ecb_encrypt(&aes_tweak, tweak, enc_tweak);

        /* Process each 16-byte sub-block */
        int i;
        for (i = 0; i < ENCRYPT_BLOCK_SIZE; i += 16) {
            /* XOR with tweak */
            int j;
            for (j = 0; j < 16; j++) {
                block[i + j] ^= enc_tweak[j];
            }

            /* Encrypt with data key */
            aes_ecb_encrypt(&aes_data, block + i, block + i);

            /* XOR with tweak again */
            for (j = 0; j < 16; j++) {
                block[i + j] ^= enc_tweak[j];
            }

            /* Update tweak for next sub-block */
            gf128_mul_alpha(enc_tweak);
        }

        /* Reverse output bytes (espsecure does this) */
        reverse_bytes(block, ENCRYPT_BLOCK_SIZE);

        /* Copy output */
        memcpy(out_buf + offset, block, block_len);
    }

    return ENCRYPT_OK;
}

/*
 * Decrypt_Data - Decrypt data using AES-XTS
 *
 * Reference: espsecure _flash_encryption_operation_aes_xts
 */
int Decrypt_Data(ENCRYPT_CTX *ctx, const BYTE *in_buf, BYTE *out_buf, DWORD len)
{
    AES_CTX aes_data;
    AES_CTX aes_tweak;
    BYTE tweak[16];
    DWORD offset;

    if (!ctx || !in_buf || !out_buf) {
        return ENCRYPT_BAD_INPUT;
    }

    if (len % 16 != 0) {
        return ENCRYPT_BAD_INPUT;
    }

    if (len == 0) {
        return ENCRYPT_BAD_INPUT;
    }

    /* Initialize AES with key (AES-128 for 256-bit key, AES-256 for 512-bit
     * key) */
    if (ctx->key_len == 32) {
        aes_key_expand_128(&aes_data, ctx->key);
        aes_key_expand_128(&aes_tweak, ctx->key + 16);
    } else {
        aes_key_expand_256(&aes_data, ctx->key);
        aes_key_expand_256(&aes_tweak, ctx->key + 32);
    }

    /* Process each 128-byte block (espsecure implementation) */
    for (offset = 0; offset < len; offset += ENCRYPT_BLOCK_SIZE) {
        DWORD block_len = len - offset;
        if (block_len > ENCRYPT_BLOCK_SIZE) {
            block_len = ENCRYPT_BLOCK_SIZE;
        }

        /* Calculate tweak (little-endian flash address, padded to 16 bytes)
         * Reference: espsecure struct.pack("<I", (flash_address & ~0x7F)) +
         * (b"\x00" * 12)
         */
        DWORD tweak_addr = (ctx->flash_addr + offset) & ~0x7F;
        tweak[0] = (BYTE)(tweak_addr & 0xFF);
        tweak[1] = (BYTE)((tweak_addr >> 8) & 0xFF);
        tweak[2] = (BYTE)((tweak_addr >> 16) & 0xFF);
        tweak[3] = (BYTE)((tweak_addr >> 24) & 0xFF);
        memset(tweak + 4, 0, 12);

        /* Copy input block */
        BYTE block[ENCRYPT_BLOCK_SIZE];
        memcpy(block, in_buf + offset, block_len);
        if (block_len < ENCRYPT_BLOCK_SIZE) {
            memset(block + block_len, 0, ENCRYPT_BLOCK_SIZE - block_len);
        }

        /* Reverse input bytes (espsecure does this) */
        reverse_bytes(block, ENCRYPT_BLOCK_SIZE);

        /* Encrypt tweak */
        BYTE enc_tweak[16];
        aes_ecb_encrypt(&aes_tweak, tweak, enc_tweak);

        /* Process each 16-byte sub-block */
        int i;
        for (i = 0; i < ENCRYPT_BLOCK_SIZE; i += 16) {
            /* XOR with tweak */
            int j;
            for (j = 0; j < 16; j++) {
                block[i + j] ^= enc_tweak[j];
            }

            /* Decrypt with data key */
            aes_ecb_decrypt(&aes_data, block + i, block + i);

            /* XOR with tweak again */
            for (j = 0; j < 16; j++) {
                block[i + j] ^= enc_tweak[j];
            }

            /* Update tweak for next sub-block */
            gf128_mul_alpha(enc_tweak);
        }

        /* Reverse output bytes (espsecure does this) */
        reverse_bytes(block, ENCRYPT_BLOCK_SIZE);

        /* Copy output */
        memcpy(out_buf + offset, block, block_len);
    }

    return ENCRYPT_OK;
}
