/*
 * test_chip.c - Unit tests for ESP chip simulation
 *
 * Tests chip init/close, property getters/setters, register access,
 * and MAC address operations for all supported chip types.
 */

#include "../src/fesptool/chip.h"
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

/* ========================================================================
 * Init / Close lifecycle
 * ======================================================================== */

static void test_init_all_chip_types(void)
{
    printf("\nTest: Init all chip types\n");

    fesp_chip_type_t types[] = {FESP_CHIP_ESP8266,   FESP_CHIP_ESP32,
                                FESP_CHIP_ESP32S2,    FESP_CHIP_ESP32S3,
                                FESP_CHIP_ESP32C2,    FESP_CHIP_ESP32C3,
                                FESP_CHIP_ESP32C6};

    for (int i = 0; i < 7; i++) {
        fesp_chip_ctx_t ctx;
        bool ok = fesp_chip_init(&ctx, types[i]);
        TEST_ASSERT(ok, "Init succeeds");
        TEST_ASSERT(ctx.efuse != NULL, "eFuse allocated");
        TEST_ASSERT(ctx.efuse_size > 0, "eFuse size > 0");
        fesp_chip_close(&ctx);
        TEST_ASSERT(ctx.efuse == NULL, "eFuse freed after close");
    }
}

static void test_close_without_init(void)
{
    printf("\nTest: Close without init\n");

    fesp_chip_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    fesp_chip_close(&ctx); /* Should not crash */
    TEST_ASSERT(true, "Close on zeroed ctx does not crash");
}

/* ========================================================================
 * Chip properties
 * ======================================================================== */

static void test_chip_name_esp32(void)
{
    printf("\nTest: Chip name ESP32\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    TEST_ASSERT(strcmp(fesp_chip_get_name(&ctx), "ESP32") == 0,
                "Name is ESP32");
    fesp_chip_close(&ctx);
}

static void test_chip_name_esp32s3(void)
{
    printf("\nTest: Chip name ESP32-S3\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32S3);
    TEST_ASSERT(strcmp(fesp_chip_get_name(&ctx), "ESP32-S3") == 0,
                "Name is ESP32-S3");
    fesp_chip_close(&ctx);
}

static void test_chip_id(void)
{
    printf("\nTest: Chip ID\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    uint32_t id = fesp_chip_get_chip_id(&ctx);
    TEST_ASSERT(id != 0, "Chip ID is non-zero");
    TEST_ASSERT(ctx.chip_id == id, "chip_id field matches getter");
    fesp_chip_close(&ctx);
}

static void test_efuse_size(void)
{
    printf("\nTest: eFuse size\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    int size = fesp_chip_get_efuse_size(&ctx);
    TEST_ASSERT(size == ctx.efuse_size, "efuse_size matches");
    TEST_ASSERT(size > 0, "eFuse size > 0");
    fesp_chip_close(&ctx);
}

static void test_efuse_accessors(void)
{
    printf("\nTest: eFuse accessors\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    const uint8_t *ro = fesp_chip_get_efuse(&ctx);
    uint8_t *rw = fesp_chip_get_efuse_mut(&ctx);
    TEST_ASSERT(ro != NULL, "Read-only eFuse not NULL");
    TEST_ASSERT(rw != NULL, "Mutable eFuse not NULL");
    TEST_ASSERT(ro == (const uint8_t *)rw, "Both point to same memory");
    fesp_chip_close(&ctx);
}

static void test_flash_size_default(void)
{
    printf("\nTest: Default flash size\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    uint32_t size = fesp_chip_get_flash_size(&ctx);
    TEST_ASSERT(size == 4 * 1024 * 1024, "Default is 4MB");
    fesp_chip_close(&ctx);
}

static void test_set_flash_size(void)
{
    printf("\nTest: Set flash size\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    fesp_chip_set_flash_size(&ctx, 2 * 1024 * 1024);
    TEST_ASSERT(fesp_chip_get_flash_size(&ctx) == 2 * 1024 * 1024,
                "Flash size updated to 2MB");
    fesp_chip_close(&ctx);
}

static void test_boot_baud_rate(void)
{
    printf("\nTest: Boot baud rate\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    uint32_t baud = fesp_chip_get_boot_baud_rate(&ctx);
    TEST_ASSERT(baud == 115200 || baud == 74880,
                "Boot baud rate is 115200 or 74880");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * MAC address
 * ======================================================================== */

static void test_default_mac(void)
{
    printf("\nTest: Default MAC\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    const uint8_t *mac = fesp_chip_get_mac(&ctx);
    TEST_ASSERT(mac[0] == 0xAA, "MAC[0] = 0xAA");
    TEST_ASSERT(mac[1] == 0xBB, "MAC[1] = 0xBB");
    TEST_ASSERT(mac[2] == 0xCC, "MAC[2] = 0xCC");
    TEST_ASSERT(mac[3] == 0xDD, "MAC[3] = 0xDD");
    TEST_ASSERT(mac[4] == 0xEE, "MAC[4] = 0xEE");
    TEST_ASSERT(mac[5] == 0x01, "MAC[5] = 0x01");
    fesp_chip_close(&ctx);
}

static void test_set_mac(void)
{
    printf("\nTest: Set MAC\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    uint8_t new_mac[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bool ok = fesp_chip_set_mac(&ctx, new_mac);
    TEST_ASSERT(ok, "Set MAC returns true");

    const uint8_t *mac = fesp_chip_get_mac(&ctx);
    TEST_ASSERT(memcmp(mac, new_mac, 6) == 0, "MAC matches");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Register access
 * ======================================================================== */

static void test_read_reg_chip_detect(void)
{
    printf("\nTest: Read chip detect reg\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    uint32_t val = fesp_chip_read_reg(&ctx, 0x40001000);
    TEST_ASSERT(val == ctx.chip_id, "Read 0x40001000 returns chip_id");
    fesp_chip_close(&ctx);
}

static void test_read_reg_unmapped(void)
{
    printf("\nTest: Read unmapped reg\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    uint32_t val = fesp_chip_read_reg(&ctx, 0xDEADBEEF);
    TEST_ASSERT(val == 0, "Unmapped address returns 0");
    fesp_chip_close(&ctx);
}

static void test_write_read_reg(void)
{
    printf("\nTest: Write then read SPI reg (ESP32-S3)\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32S3);
    /* 0x60002000 is SPI reg base for S3, offset 0x04 is SPI_USR */
    bool ok = fesp_chip_write_reg(&ctx, 0x60002004, 0xDEADBEEF);
    TEST_ASSERT(ok, "Write returns true");

    uint32_t val = fesp_chip_read_reg(&ctx, 0x60002004);
    TEST_ASSERT(val == 0xDEADBEEF, "Read back matches written value");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Boot message
 * ======================================================================== */

static void test_boot_message_normal(void)
{
    printf("\nTest: Boot message (normal)\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    char buf[256] = {0};
    const char *msg = fesp_chip_get_boot_message(&ctx, false, 0, buf,
                                                 sizeof(buf));
    TEST_ASSERT(msg != NULL, "Boot message not NULL");
    TEST_ASSERT(strlen(msg) > 0, "Boot message not empty");
    fesp_chip_close(&ctx);
}

static void test_boot_message_download(void)
{
    printf("\nTest: Boot message (download)\n");

    fesp_chip_ctx_t ctx;
    fesp_chip_init(&ctx, FESP_CHIP_ESP32);
    char buf[256] = {0};
    const char *msg = fesp_chip_get_boot_message(&ctx, true, 0, buf,
                                                 sizeof(buf));
    TEST_ASSERT(msg != NULL, "Download message not NULL");
    TEST_ASSERT(strlen(msg) > 0, "Download message not empty");
    fesp_chip_close(&ctx);
}

/* ========================================================================
 * Main test runner
 * ======================================================================== */

int main(void)
{
    printf("=== ESP Chip Simulation Tests ===\n");

    test_init_all_chip_types();
    test_close_without_init();

    test_chip_name_esp32();
    test_chip_name_esp32s3();
    test_chip_id();
    test_efuse_size();
    test_efuse_accessors();
    test_flash_size_default();
    test_set_flash_size();
    test_boot_baud_rate();

    test_default_mac();
    test_set_mac();

    test_read_reg_chip_detect();
    test_read_reg_unmapped();
    test_write_read_reg();

    test_boot_message_normal();
    test_boot_message_download();

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
