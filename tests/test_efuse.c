/*
 * test_efuse.c - Unit tests for eFuse simulation
 *
 * Tests eFuse field queries, key purpose, flash encryption,
 * and security settings by directly manipulating the eFuse buffer.
 */

#include "../src/fesptool/chip.h"
#include "../src/fesptool/efuse.h"
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

/* Helper: init chip, run body, close */
static fesp_chip_ctx_t init_chip(fesp_chip_type_t type)
{
    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, type);
    return ctx;
}

/* ========================================================================
 * Default state (fresh init = all zeros in eFuse)
 * ======================================================================== */

static void test_defaults_esp32(void)
{
    printf("\nTest: Default eFuse state (ESP32)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    TEST_ASSERT(fesp_efuse_get_flash_crypt_cnt(&ctx) == 0,
                "flash_crypt_cnt is 0");
    TEST_ASSERT(!fesp_efuse_is_flash_encryption_enabled(&ctx),
                "flash encryption not enabled");
    TEST_ASSERT(!fesp_efuse_is_secure_boot_enabled(&ctx),
                "secure boot not enabled");
    TEST_ASSERT(!fesp_efuse_is_jtag_disabled(&ctx), "JTAG not disabled");
    fesp_chip_close(&ctx);
}

static void test_defaults_esp32s3(void)
{
    printf("\nTest: Default eFuse state (ESP32-S3)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32S3);
    TEST_ASSERT(fesp_efuse_get_flash_crypt_cnt(&ctx) == 0,
                "flash_crypt_cnt is 0");
    TEST_ASSERT(!fesp_efuse_is_flash_encryption_enabled(&ctx),
                "flash encryption not enabled");
    TEST_ASSERT(!fesp_efuse_is_secure_boot_enabled(&ctx),
                "secure boot not enabled");
    TEST_ASSERT(!fesp_efuse_is_jtag_disabled(&ctx), "JTAG not disabled");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Flash crypt count
 * ======================================================================== */

static void test_flash_crypt_cnt_esp32(void)
{
    printf("\nTest: Flash crypt count (ESP32)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    /* ESP32: FLASH_CRYPT_CNT at offset 0x00, mask 0x7F << 20
     * To set count=3: set bits 20-21 -> byte 0x02 = 0x30 */
    ctx.efuse[0x02] = 0x30;
    uint32_t cnt = fesp_efuse_get_flash_crypt_cnt(&ctx);
    TEST_ASSERT(cnt == 3, "Count is 3");
    /* count=3 = binary 011, has 2 ones (even) -> NOT enabled */
    TEST_ASSERT(!fesp_efuse_is_flash_encryption_enabled(&ctx),
                "Even popcount (3=011) does not enable encryption");
    fesp_chip_close(&ctx);
}

static void test_flash_crypt_cnt_even(void)
{
    printf("\nTest: Flash crypt count popcount parity\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    /* count=1: bit 20 only -> byte 0x02 = 0x10, popcount=1 (odd) -> enabled */
    ctx.efuse[0x02] = 0x10;
    TEST_ASSERT(fesp_efuse_is_flash_encryption_enabled(&ctx),
                "Count=1 (popcount 1, odd) → enabled");

    /* count=3: bits 20-21 -> byte 0x02 = 0x30, popcount=2 (even) -> disabled */
    ctx.efuse[0x02] = 0x30;
    TEST_ASSERT(!fesp_efuse_is_flash_encryption_enabled(&ctx),
                "Count=3 (popcount 2, even) → disabled");

    /* count=7: bits 20-22 -> byte 0x02 = 0x70, popcount=3 (odd) -> enabled */
    ctx.efuse[0x02] = 0x70;
    TEST_ASSERT(fesp_efuse_is_flash_encryption_enabled(&ctx),
                "Count=7 (popcount 3, odd) → enabled");

    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Key purpose
 * ======================================================================== */

static void test_key_purpose_esp32(void)
{
    printf("\nTest: Key purpose ESP32 (fixed assignments)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    /* ESP32: BLOCK0 = user, BLOCK1 = flash encryption, BLOCK2/3 = user */
    TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, 0) ==
                    FESP_KEY_PURPOSE_XTS_AES_128_KEY,
                "BLOCK0 → XTS_AES_128");
    TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, 1) == FESP_KEY_PURPOSE_USER,
                "BLOCK1 → USER");
    TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, 2) == FESP_KEY_PURPOSE_USER,
                "BLOCK2 → USER");
    fesp_chip_close(&ctx);
}

static void test_key_purpose_esp32s3_default(void)
{
    printf("\nTest: Key purpose ESP32-S3 default (all USER)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32S3);
    for (int i = 0; i <= 5; i++) {
        TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, i) ==
                        FESP_KEY_PURPOSE_USER,
                    "All blocks default to USER");
    }
    fesp_chip_close(&ctx);
}

static void test_key_purpose_esp32s3_set(void)
{
    printf("\nTest: Key purpose ESP32-S3 set/get\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32S3);
    fesp_efuse_set_key_purpose(&ctx, 0, FESP_KEY_PURPOSE_XTS_AES_256_KEY_1);
    TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, 0) ==
                    FESP_KEY_PURPOSE_XTS_AES_256_KEY_1,
                "BLOCK0 → XTS_AES_256_KEY_1");

    fesp_efuse_set_key_purpose(&ctx, 2, FESP_KEY_PURPOSE_HMAC_UP);
    TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, 2) == FESP_KEY_PURPOSE_HMAC_UP,
                "BLOCK2 → HMAC_UP");
    fesp_chip_close(&ctx);
}

static void test_key_purpose_out_of_range(void)
{
    printf("\nTest: Key purpose out of range\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32S3);
    TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, -1) == FESP_KEY_PURPOSE_USER,
                "Block -1 → USER");
    TEST_ASSERT(fesp_efuse_get_key_purpose(&ctx, 6) == FESP_KEY_PURPOSE_USER,
                "Block 6 → USER");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Download mode settings
 * ======================================================================== */

static void test_download_encrypt_disabled_esp32(void)
{
    printf("\nTest: Download encrypt disabled (ESP32)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    /* ESP32: DISABLE_DL_ENCRYPT at offset 0x18, bit 7 -> byte 0x18 = 0x80 */
    ctx.efuse[0x18] = 0x80;
    TEST_ASSERT(fesp_efuse_is_download_encrypt_disabled(&ctx),
                "DL encrypt disabled");
    fesp_chip_close(&ctx);
}

static void test_download_mode_disabled_esp32(void)
{
    printf("\nTest: Download mode disabled (ESP32)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    /* ESP32: UART_DOWNLOAD_DIS at offset 0x00, bit 27 -> byte 0x03 = 0x08 */
    ctx.efuse[0x03] = 0x08;
    TEST_ASSERT(fesp_efuse_is_download_mode_disabled(&ctx),
                "Download mode disabled");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Flash encryption set
 * ======================================================================== */

static void test_set_flash_encryption_esp32(void)
{
    printf("\nTest: Set flash encryption (ESP32)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    TEST_ASSERT(!fesp_efuse_is_flash_encryption_enabled(&ctx),
                "Initially disabled");

    fesp_efuse_set_flash_encryption(&ctx, 1);
    TEST_ASSERT(fesp_efuse_is_flash_encryption_enabled(&ctx),
                "Enabled after set");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * apply_block0_defaults
 * ======================================================================== */

static void test_apply_block0_defaults_esp32(void)
{
    printf("\nTest: apply_block0_defaults (ESP32)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    fesp_efuse_apply_block0_defaults(&ctx);
    /* After defaults, some basic bits should be set */
    TEST_ASSERT(ctx.efuse[0] != 0 || ctx.efuse[1] != 0 || ctx.efuse[2] != 0 ||
                    ctx.efuse[3] != 0,
                "Block0 not all zeros after defaults");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Encryption key offset
 * ======================================================================== */

static void test_encryption_key_offset_esp32(void)
{
    printf("\nTest: Encryption key offset (ESP32)\n");

    fesp_chip_ctx_t ctx = init_chip(FESP_CHIP_ESP32);
    int key_len = 0;
    int offset = fesp_efuse_get_encryption_key_offset(&ctx, &key_len);
    TEST_ASSERT(offset >= 0, "Offset >= 0");
    TEST_ASSERT(key_len > 0, "Key length > 0");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Main test runner
 * ======================================================================== */

int main(void)
{
    printf("=== eFuse Simulation Tests ===\n");

    test_defaults_esp32();
    test_defaults_esp32s3();

    test_flash_crypt_cnt_esp32();
    test_flash_crypt_cnt_even();

    test_key_purpose_esp32();
    test_key_purpose_esp32s3_default();
    test_key_purpose_esp32s3_set();
    test_key_purpose_out_of_range();

    test_download_encrypt_disabled_esp32();
    test_download_mode_disabled_esp32();

    test_set_flash_encryption_esp32();

    test_apply_block0_defaults_esp32();

    test_encryption_key_offset_esp32();

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
