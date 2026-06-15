/*
 * test_encrypt.c - Unit tests for AES-XTS encryption/decryption
 *
 * Tests the AES-XTS encryptor/decryptor with various key sizes and edge cases.
 * All ESP chips use standard AES-XTS with 16-byte blocks.
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../src/utils/encrypt.h"
#include "encrypt_test_data.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/* Test helper macro */
#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            printf("  PASS: %s\n", message); \
            tests_passed++; \
        } else { \
            printf("  FAIL: %s\n", message); \
            tests_failed++; \
        } \
    } while (0)

/* Test 0: AES-ECB with NIST test vector */
static void test_aes_ecb_nist(void)
{
    printf("\nTest 0: AES-ECB with NIST test vector\n");

    /* NIST AES-128 test vector */
    BYTE nist_key[16] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                         0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
    BYTE nist_plaintext[16] = {0x32, 0x43, 0xF6, 0xA8, 0x88, 0x5A, 0x30, 0x8D,
                               0x31, 0x31, 0x98, 0xA2, 0xE0, 0x37, 0x07, 0x34};
    BYTE nist_expected[16] = {0x39, 0x25, 0x84, 0x1D, 0x02, 0xDC, 0x09, 0xFB,
                              0xDC, 0x11, 0x85, 0x97, 0x19, 0x6A, 0x0B, 0x32};

    /* We can't directly test AES-ECB from here since it's a static function */
    /* But we can test through the XTS interface */
    /* For now, just print that this test exists */
    printf("  NIST test vector defined (AES-128-ECB)\n");
    printf("  Key:       ");
    int i;
    for (i = 0; i < 16; i++)
        printf("%02X ", nist_key[i]);
    printf("\n");
    printf("  Plaintext: ");
    for (i = 0; i < 16; i++)
        printf("%02X ", nist_plaintext[i]);
    printf("\n");
    printf("  Expected:  ");
    for (i = 0; i < 16; i++)
        printf("%02X ", nist_expected[i]);
    printf("\n");
}

/* Test 1: Basic encrypt/decrypt with 256-bit key */
static void test_basic_encrypt_decrypt(void)
{
    printf("\nTest 1: Basic encrypt/decrypt with 256-bit key\n");

    /* NIST AES-128 test vector */
    BYTE nist_key[16] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                         0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
    BYTE nist_plaintext[16] = {0x32, 0x43, 0xF6, 0xA8, 0x88, 0x5A, 0x30, 0x8D,
                               0x31, 0x31, 0x98, 0xA2, 0xE0, 0x37, 0x07, 0x34};
    BYTE nist_expected[16] = {0x39, 0x25, 0x84, 0x1D, 0x02, 0xDC, 0x09, 0xFB,
                              0xDC, 0x11, 0x85, 0x97, 0x19, 0x6A, 0x0B, 0x32};
    
    /* Test NIST vector with single 16-byte key (for AES-128) */
    /* We'll use this to verify our AES implementation */
    
    BYTE ciphertext[16] = {0};
    BYTE decrypted[16] = {0};
    ENCRYPT_CTX ctx;

    /* Test initialization with 256-bit key */
    int ret = Encrypt_Init(&ctx, key_xts_256, 32, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");

    /* Test encryption */
    ret = Encrypt_Data(&ctx, plaintext_16, ciphertext, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Encrypt returns OK");

    /* Debug: output ciphertext */
    printf("  Ciphertext: ");
    int i;
    for (i = 0; i < 16; i++)
        printf("%02X ", ciphertext[i]);
    printf("\n");

    /* Test decryption */
    ret = Decrypt_Data(&ctx, ciphertext, decrypted, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Decrypt returns OK");

    /* Debug: output decrypted */
    printf("  Decrypted:  ");
    for (i = 0; i < 16; i++)
        printf("%02X ", decrypted[i]);
    printf("\n");

    /* Debug: output expected */
    printf("  Expected:   ");
    for (i = 0; i < 16; i++)
        printf("%02X ", plaintext_16[i]);
    printf("\n");

    TEST_ASSERT(memcmp(decrypted, plaintext_16, 16) == 0, "Decrypted matches original plaintext");
}

/* Test 2: Encrypt/decrypt with 512-bit key */
static void test_512bit_key(void)
{
    printf("\nTest 2: Encrypt/decrypt with 512-bit key\n");

    BYTE ciphertext[16] = {0};
    BYTE decrypted[16] = {0};
    ENCRYPT_CTX ctx;

    /* Test initialization */
    int ret = Encrypt_Init(&ctx, key_xts_512, 64, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");

    /* Test encryption */
    ret = Encrypt_Data(&ctx, plaintext_16, ciphertext, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Encrypt returns OK");
    TEST_ASSERT(memcmp(ciphertext, plaintext_16, 16) != 0, "Ciphertext differs from plaintext");

    /* Test decryption */
    ret = Decrypt_Data(&ctx, ciphertext, decrypted, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Decrypt returns OK");
    TEST_ASSERT(memcmp(decrypted, plaintext_16, 16) == 0, "Decrypted matches original plaintext");
}

/* Test 3: Multiple blocks (48 bytes) */
static void test_multiple_blocks(void)
{
    printf("\nTest 3: Multiple blocks (48 bytes)\n");

    BYTE ciphertext[48] = {0};
    BYTE decrypted[48] = {0};
    ENCRYPT_CTX ctx;

    /* Test initialization */
    int ret = Encrypt_Init(&ctx, key_xts_256, 32, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");

    /* Test encryption */
    ret = Encrypt_Data(&ctx, plaintext_48, ciphertext, 48);
    TEST_ASSERT(ret == ENCRYPT_OK, "Encrypt returns OK");
    TEST_ASSERT(memcmp(ciphertext, plaintext_48, 48) != 0, "Ciphertext differs from plaintext");

    /* Test decryption */
    ret = Decrypt_Data(&ctx, ciphertext, decrypted, 48);
    TEST_ASSERT(ret == ENCRYPT_OK, "Decrypt returns OK");
    TEST_ASSERT(memcmp(decrypted, plaintext_48, 48) == 0, "Decrypted matches original plaintext");
}

/* Test 4: Different flash addresses produce different ciphertext */
static void test_different_addresses(void)
{
    printf("\nTest 4: Different flash addresses produce different ciphertext\n");

    BYTE ciphertext1[16] = {0};
    BYTE ciphertext2[16] = {0};
    ENCRYPT_CTX ctx;

    /* Encrypt at address 0x10000 */
    int ret = Encrypt_Init(&ctx, key_xts_256, 32, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");
    ret = Encrypt_Data(&ctx, plaintext_16, ciphertext1, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Encrypt at 0x10000 returns OK");

    /* Encrypt at address 0x20000 */
    ret = Encrypt_Init(&ctx, key_xts_256, 32, 0x20000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");
    ret = Encrypt_Data(&ctx, plaintext_16, ciphertext2, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Encrypt at 0x20000 returns OK");

    /* Ciphertext should be different */
    TEST_ASSERT(memcmp(ciphertext1, ciphertext2, 16) != 0,
                "Different addresses produce different ciphertext");
}

/* Test 5: Invalid parameters */
static void test_invalid_params(void)
{
    printf("\nTest 5: Invalid parameters\n");

    BYTE key[32] = {0};
    BYTE buf[16] = {0};
    ENCRYPT_CTX ctx;

    /* Test NULL key */
    int ret = Encrypt_Init(&ctx, NULL, 32, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_BAD_INPUT, "NULL key returns BAD_INPUT");

    /* Test invalid key length */
    ret = Encrypt_Init(&ctx, key, 24, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_BAD_INPUT, "Invalid key length returns BAD_INPUT");

    /* Test unaligned flash address */
    ret = Encrypt_Init(&ctx, key, 32, 0x10001);
    TEST_ASSERT(ret == ENCRYPT_BAD_INPUT, "Unaligned flash address returns BAD_INPUT");

    /* Test NULL buffers */
    ret = Encrypt_Init(&ctx, key, 32, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");

    ret = Encrypt_Data(&ctx, NULL, buf, 16);
    TEST_ASSERT(ret == ENCRYPT_BAD_INPUT, "NULL input returns BAD_INPUT");

    ret = Encrypt_Data(&ctx, buf, NULL, 16);
    TEST_ASSERT(ret == ENCRYPT_BAD_INPUT, "NULL output returns BAD_INPUT");

    /* Test zero length */
    ret = Encrypt_Data(&ctx, buf, buf, 0);
    TEST_ASSERT(ret == ENCRYPT_BAD_INPUT, "Zero length returns BAD_INPUT");

    /* Test unaligned length */
    ret = Encrypt_Data(&ctx, buf, buf, 15);
    TEST_ASSERT(ret == ENCRYPT_BAD_INPUT, "Unaligned length returns BAD_INPUT");
}

/* Test 6: AES-XTS compatibility with espsecure (256-bit key, 16 bytes) */
static void test_xts_256_compat_16(void)
{
    printf("\nTest 6: AES-XTS compatibility with espsecure (256-bit key, 16 bytes)\n");

    BYTE decrypted[16] = {0};
    ENCRYPT_CTX ctx;

    /* Initialize with 256-bit key */
    int ret = Encrypt_Init(&ctx, key_xts_256, 32, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");

    /* Decrypt espsecure encrypted data */
    ret = Decrypt_Data(&ctx, encrypted_xts_256_16, decrypted, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Decrypt returns OK");

    /* Verify decrypted matches original plaintext */
    TEST_ASSERT(memcmp(decrypted, plaintext_16, 16) == 0,
                "Decrypted matches original plaintext");
}

/* Test 7: AES-XTS compatibility with espsecure (256-bit key, 48 bytes) */
static void test_xts_256_compat_48(void)
{
    printf("\nTest 7: AES-XTS compatibility with espsecure (256-bit key, 48 bytes)\n");

    BYTE decrypted[48] = {0};
    ENCRYPT_CTX ctx;

    /* Initialize with 256-bit key */
    int ret = Encrypt_Init(&ctx, key_xts_256, 32, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");

    /* Decrypt espsecure encrypted data */
    ret = Decrypt_Data(&ctx, encrypted_xts_256_48, decrypted, 48);
    TEST_ASSERT(ret == ENCRYPT_OK, "Decrypt returns OK");

    /* Verify decrypted matches original plaintext */
    TEST_ASSERT(memcmp(decrypted, plaintext_48, 48) == 0,
                "Decrypted matches original plaintext");
}

/* Test 8: AES-XTS compatibility with espsecure (512-bit key, 16 bytes) */
static void test_xts_512_compat_16(void)
{
    printf("\nTest 8: AES-XTS compatibility with espsecure (512-bit key, 16 bytes)\n");

    BYTE decrypted[16] = {0};
    ENCRYPT_CTX ctx;

    /* Initialize with 512-bit key */
    int ret = Encrypt_Init(&ctx, key_xts_512, 64, 0x10000);
    TEST_ASSERT(ret == ENCRYPT_OK, "Init returns OK");

    /* Decrypt espsecure encrypted data */
    ret = Decrypt_Data(&ctx, encrypted_xts_512_16, decrypted, 16);
    TEST_ASSERT(ret == ENCRYPT_OK, "Decrypt returns OK");

    /* Verify decrypted matches original plaintext */
    TEST_ASSERT(memcmp(decrypted, plaintext_16, 16) == 0,
                "Decrypted matches original plaintext");
}

/* Main test runner */
int main(void)
{
    printf("=== AES-XTS Encryption/Decryption Tests ===\n");

    /* AES-ECB verification */
    test_aes_ecb_nist();

    /* Basic functionality tests */
    test_basic_encrypt_decrypt();
    test_512bit_key();
    test_multiple_blocks();
    test_different_addresses();
    test_invalid_params();

    /* Compatibility tests with espsecure */
    test_xts_256_compat_16();
    test_xts_256_compat_48();
    test_xts_512_compat_16();

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
