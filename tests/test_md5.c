/*
 * test_md5.c - Unit tests for MD5 hash (RFC 1321 test vectors)
 */

#include "../src/utils/md5.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (condition) {                                                       \
            printf("  PASS: %s\n", message);                                   \
            tests_passed++;                                                    \
        } else {                                                               \
            printf("  FAIL: %s\n", message);                                   \
            tests_failed++;                                                    \
        }                                                                      \
    } while (0)

static void hex_to_bytes(const char *hex, BYTE *out, int out_len)
{
    for (int i = 0; i < out_len; i++) {
        unsigned int b;
        sscanf(hex + i * 2, "%02x", &b);
        out[i] = (BYTE)b;
    }
}

static void test_hash(const char *input, int len, const char *expected_hex,
                      const char *desc)
{
    printf("\n%s\n", desc);

    BYTE digest[MD5_DIGEST_SIZE];
    BYTE expected[MD5_DIGEST_SIZE];

    MD5_Calc((const BYTE *)input, len, digest);
    hex_to_bytes(expected_hex, expected, MD5_DIGEST_SIZE);

    char got_hex[33] = {0};
    for (int i = 0; i < MD5_DIGEST_SIZE; i++) {
        sprintf(got_hex + i * 2, "%02x", digest[i]);
    }

    printf("  Expected: %s\n", expected_hex);
    printf("  Got:      %s\n", got_hex);
    TEST_ASSERT(memcmp(digest, expected, MD5_DIGEST_SIZE) == 0,
                "Digest matches RFC 1321");
}

/* RFC 1321 Appendix A.5 - Test suite */
static void test_rfc1321_empty(void)
{
    test_hash("", 0, "d41d8cd98f00b204e9800998ecf8427e",
              "RFC 1321: empty string");
}

static void test_rfc1321_a(void)
{
    test_hash("a", 1, "0cc175b9c0f1b6a831c399e269772661", "RFC 1321: \"a\"");
}

static void test_rfc1321_abc(void)
{
    test_hash("abc", 3, "900150983cd24fb0d6963f7d28e17f72",
              "RFC 1321: \"abc\"");
}

static void test_rfc1321_message_digest(void)
{
    test_hash("message digest", 14, "f96b697d7cb7938d525a2f31aaf161d0",
              "RFC 1321: \"message digest\"");
}

static void test_rfc1321_lowercase(void)
{
    test_hash("abcdefghijklmnopqrstuvwxyz", 26,
              "c3fcd3d76192e4007dfb496cca67e13b",
              "RFC 1321: \"abcdefghijklmnopqrstuvwxyz\"");
}

static void test_rfc1321_alphanumeric(void)
{
    test_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
              62, "d174ab98d277d9f5a5611c2c9f419d9f", "RFC 1321: alphanumeric");
}

static void test_rfc1321_digits(void)
{
    test_hash("1234567890123456789012345678901234567890123456789012345678901234"
              "5678901234567890",
              80, "57edf4a22be3c955ac49da2e2107b67a", "RFC 1321: digits");
}

/* Binary test vectors from RFC 1321 Appendix A.5 */
static void test_rfc1321_one_byte_zero(void)
{
    BYTE input = 0x00;
    test_hash((const char *)&input, 1, "93b885adfe0da089cdf634904fd59f71",
              "RFC 1321: single 0x00 byte");
}

static void test_rfc1321_64_zero_bytes(void)
{
    BYTE input[64];
    memset(input, 0x00, sizeof(input));
    test_hash((const char *)input, 64, "3b5d3c7d207e37dceeedd301e35e2e58",
              "RFC 1321: 64 × 0x00");
}

static void test_rfc1321_64_ff_bytes(void)
{
    BYTE input[64];
    memset(input, 0xFF, sizeof(input));
    test_hash((const char *)input, 64, "aabd2b2a451504e119a243d8e775fdad",
              "RFC 1321: 64 × 0xFF");
}

/* Additional edge cases */
static void test_single_byte_values(void)
{
    printf("\nSingle-byte hash values (spot check)\n");

    BYTE digest[MD5_DIGEST_SIZE];

    MD5_Calc((const BYTE *)"0", 1, digest);
    TEST_ASSERT(digest[0] == 0xcf, "MD5(\"0\") first byte is 0xcf");

    MD5_Calc((const BYTE *)"\x80", 1, digest);
    TEST_ASSERT(digest[0] == 0x8d, "MD5(0x80) first byte is 0x8d");
}

static void test_consistency(void)
{
    printf("\nSame input produces same hash\n");

    BYTE d1[MD5_DIGEST_SIZE], d2[MD5_DIGEST_SIZE];

    MD5_Calc((const BYTE *)"test", 4, d1);
    MD5_Calc((const BYTE *)"test", 4, d2);

    TEST_ASSERT(memcmp(d1, d2, MD5_DIGEST_SIZE) == 0,
                "Two calls with same input match");
}

static void test_different_input(void)
{
    printf("\nDifferent input produces different hash\n");

    BYTE d1[MD5_DIGEST_SIZE], d2[MD5_DIGEST_SIZE];

    MD5_Calc((const BYTE *)"test", 4, d1);
    MD5_Calc((const BYTE *)"Test", 4, d2);

    TEST_ASSERT(memcmp(d1, d2, MD5_DIGEST_SIZE) != 0,
                "Different input → different digest");
}

int main(void)
{
    printf("=== MD5 Hash Unit Tests (RFC 1321) ===\n");

    test_rfc1321_empty();
    test_rfc1321_a();
    test_rfc1321_abc();
    test_rfc1321_message_digest();
    test_rfc1321_lowercase();
    test_rfc1321_alphanumeric();
    test_rfc1321_digits();
    test_rfc1321_one_byte_zero();
    test_rfc1321_64_zero_bytes();
    test_rfc1321_64_ff_bytes();
    test_single_byte_values();
    test_consistency();
    test_different_input();

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
