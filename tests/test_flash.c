/*
 * test_flash.c - Unit tests for Flash storage simulation
 *
 * Tests read/write/erase operations, AND-write semantics, sector alignment,
 * and boundary conditions.
 */

#include "../src/fesptool/flash.h"
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

static void test_init_and_close(void)
{
    printf("\nTest: Init and close\n");

    fesp_flash_ctx_t ctx;
    bool ok = fesp_flash_init(&ctx, 4096);
    TEST_ASSERT(ok, "Init returns true");
    TEST_ASSERT(ctx.data != NULL, "Data pointer is set");
    TEST_ASSERT(ctx.size == 4096, "Size is 4096");

    /* Verify erased state (0xFF) */
    for (uint32_t i = 0; i < 4096; i++) {
        if (ctx.data[i] != 0xFF) {
            TEST_ASSERT(false, "All bytes initialized to 0xFF");
            goto done;
        }
    }
    TEST_ASSERT(true, "All bytes initialized to 0xFF");

done:
    fesp_flash_close(&ctx);
    TEST_ASSERT(ctx.data == NULL, "Data pointer cleared after close");
}

static void test_init_zero_size(void)
{
    printf("\nTest: Init with zero size\n");

    fesp_flash_ctx_t ctx;
    bool ok = fesp_flash_init(&ctx, 0);
    TEST_ASSERT(!ok, "Init with size=0 returns false");
}

static void test_close_without_init(void)
{
    printf("\nTest: Close without init (NULL data)\n");

    fesp_flash_ctx_t ctx = {.data = NULL, .size = 0};
    fesp_flash_close(&ctx); /* Should not crash */
    TEST_ASSERT(true, "Close on NULL data does not crash");
}

/* ========================================================================
 * Read / Write
 * ======================================================================== */

static void test_write_and_read(void)
{
    printf("\nTest: Write and read\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    uint8_t wdata[] = {0x01, 0x02, 0x03, 0x04};
    bool ok = fesp_flash_write(&ctx, 0, wdata, 4);
    TEST_ASSERT(ok, "Write returns true");

    uint8_t rbuf[4] = {0};
    ok = fesp_flash_read(&ctx, 0, rbuf, 4);
    TEST_ASSERT(ok, "Read returns true");
    TEST_ASSERT(memcmp(rbuf, wdata, 4) == 0, "Read data matches written");

    fesp_flash_close(&ctx);
}

static void test_read_unwritten_is_ff(void)
{
    printf("\nTest: Read unwritten region returns 0xFF\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    uint8_t rbuf[8] = {0};
    fesp_flash_read(&ctx, 100, rbuf, 8);

    bool all_ff = true;
    for (int i = 0; i < 8; i++) {
        if (rbuf[i] != 0xFF) {
            all_ff = false;
            break;
        }
    }
    TEST_ASSERT(all_ff, "Unread bytes are 0xFF");

    fesp_flash_close(&ctx);
}

/* ========================================================================
 * AND-write semantics (flash behavior)
 * ======================================================================== */

static void test_write_and_semantics(void)
{
    printf("\nTest: Write AND semantics (bits only 1→0)\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    /* Flash starts as 0xFF (all 1s) */
    /* Write 0xAA = 10101010: should clear bits 7,5,3,1 */
    uint8_t wdata1[] = {0xAA};
    fesp_flash_write(&ctx, 0, wdata1, 1);

    uint8_t rbuf[1];
    fesp_flash_read(&ctx, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xAA, "First write: 0xFF & 0xAA = 0xAA");

    /* Write 0x55 = 01010101: should clear bits 6,4,2,0 */
    /* AND with existing 0xAA = 00000000 */
    uint8_t wdata2[] = {0x55};
    fesp_flash_write(&ctx, 0, wdata2, 1);

    fesp_flash_read(&ctx, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0x00, "Second write: 0xAA & 0x55 = 0x00");

    /* Cannot set bits back without erase */
    uint8_t wdata3[] = {0xFF};
    fesp_flash_write(&ctx, 0, wdata3, 1);

    fesp_flash_read(&ctx, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0x00, "Writing 0xFF cannot restore bits");

    fesp_flash_close(&ctx);
}

static void test_write_partial_overlap(void)
{
    printf("\nTest: Write at offset\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    uint8_t wdata[] = {0x01, 0x02};
    fesp_flash_write(&ctx, 10, wdata, 2);

    uint8_t rbuf[4] = {0};
    fesp_flash_read(&ctx, 8, rbuf, 4);

    TEST_ASSERT(rbuf[0] == 0xFF, "Byte at 8 unchanged (0xFF)");
    TEST_ASSERT(rbuf[1] == 0xFF, "Byte at 9 unchanged (0xFF)");
    TEST_ASSERT(rbuf[2] == 0x01, "Byte at 10 written");
    TEST_ASSERT(rbuf[3] == 0x02, "Byte at 11 written");

    fesp_flash_close(&ctx);
}

/* ========================================================================
 * Erase
 * ======================================================================== */

static void test_erase_sector(void)
{
    printf("\nTest: Erase single sector (4KB)\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 8192);

    /* Fill sector 0 with data */
    uint8_t fill[4096];
    memset(fill, 0x00, sizeof(fill));
    fesp_flash_write(&ctx, 0, fill, 4096);

    /* Verify it's not 0xFF */
    uint8_t rbuf[1];
    fesp_flash_read(&ctx, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0x00, "Sector 0 filled with 0x00");

    /* Erase sector 0 */
    fesp_flash_erase(&ctx, 0, 1);

    /* Verify erased */
    fesp_flash_read(&ctx, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "Sector 0 erased to 0xFF");

    /* Sector 1 should be untouched */
    fesp_flash_read(&ctx, 4096, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "Sector 1 still 0xFF (was never written)");

    fesp_flash_close(&ctx);
}

static void test_erase_sector_alignment(void)
{
    printf("\nTest: Erase aligns to sector boundaries\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 8192);

    /* Fill all of sector 0 and sector 1 */
    uint8_t fill[8192];
    memset(fill, 0x00, sizeof(fill));
    fesp_flash_write(&ctx, 0, fill, 8192);

    /* Erase from middle of sector 0 to middle of sector 1 */
    fesp_flash_erase(&ctx, 2048, 4096);

    /* Both sectors should be erased (alignment) */
    uint8_t rbuf[1];
    fesp_flash_read(&ctx, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "Start of sector 0 erased");

    fesp_flash_read(&ctx, 4095, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "End of sector 0 erased");

    fesp_flash_read(&ctx, 4096, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "Start of sector 1 erased");

    fesp_flash_read(&ctx, 8191, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "End of sector 1 erased");

    fesp_flash_close(&ctx);
}

static void test_erase_all(void)
{
    printf("\nTest: Erase all\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    uint8_t fill[4096];
    memset(fill, 0x00, sizeof(fill));
    fesp_flash_write(&ctx, 0, fill, 4096);

    bool ok = fesp_flash_erase_all(&ctx);
    TEST_ASSERT(ok, "Erase_all returns true");

    uint8_t rbuf[1];
    fesp_flash_read(&ctx, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "First byte erased");

    fesp_flash_read(&ctx, 4095, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "Last byte erased");

    fesp_flash_close(&ctx);
}

static void test_erase_all_without_init(void)
{
    printf("\nTest: Erase all without init\n");

    fesp_flash_ctx_t ctx = {.data = NULL, .size = 0};
    bool ok = fesp_flash_erase_all(&ctx);
    TEST_ASSERT(!ok, "Erase_all on NULL data returns false");
}

/* ========================================================================
 * Calc MD5
 * ======================================================================== */

static void test_calc_md5(void)
{
    printf("\nTest: Calc MD5\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    /* Write known data */
    uint8_t wdata[16];
    for (int i = 0; i < 16; i++) {
        wdata[i] = (uint8_t)i;
    }
    fesp_flash_write(&ctx, 0, wdata, 16);

    uint8_t md5_a[16] = {0};
    fesp_flash_calc_md5(&ctx, 0, 16, md5_a);

    /* MD5 of same data should be deterministic */
    uint8_t md5_b[16] = {0};
    fesp_flash_calc_md5(&ctx, 0, 16, md5_b);

    TEST_ASSERT(memcmp(md5_a, md5_b, 16) == 0, "Same data → same MD5");

    /* MD5 of different region should differ */
    uint8_t md5_c[16] = {0};
    fesp_flash_calc_md5(&ctx, 16, 16, md5_c);

    TEST_ASSERT(memcmp(md5_a, md5_c, 16) != 0,
                "Different region → different MD5");

    fesp_flash_close(&ctx);
}

static void test_calc_md5_invalid_region(void)
{
    printf("\nTest: Calc MD5 with invalid region\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    uint8_t md5[16];
    fesp_flash_calc_md5(&ctx, 4096, 16, md5);

    bool all_zero = true;
    for (int i = 0; i < 16; i++) {
        if (md5[i] != 0) {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT(all_zero, "Invalid region returns zeroed MD5");

    fesp_flash_close(&ctx);
}

/* ========================================================================
 * Boundary conditions
 * ======================================================================== */

static void test_read_out_of_bounds(void)
{
    printf("\nTest: Read out of bounds\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    uint8_t rbuf[16];
    bool ok;

    ok = fesp_flash_read(&ctx, 4096, rbuf, 1);
    TEST_ASSERT(!ok, "Read at size boundary returns false");

    ok = fesp_flash_read(&ctx, 4080, rbuf, 17);
    TEST_ASSERT(!ok, "Read crossing boundary returns false");

    fesp_flash_close(&ctx);
}

static void test_write_out_of_bounds(void)
{
    printf("\nTest: Write out of bounds\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    uint8_t wdata[16] = {0};
    bool ok;

    ok = fesp_flash_write(&ctx, 4096, wdata, 1);
    TEST_ASSERT(!ok, "Write at size boundary returns false");

    ok = fesp_flash_write(&ctx, 4080, wdata, 17);
    TEST_ASSERT(!ok, "Write crossing boundary returns false");

    fesp_flash_close(&ctx);
}

static void test_erase_zero_length(void)
{
    printf("\nTest: Erase with zero length\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    bool ok = fesp_flash_erase(&ctx, 0, 0);
    TEST_ASSERT(!ok, "Erase with len=0 returns false");

    fesp_flash_close(&ctx);
}

static void test_erase_at_size_boundary(void)
{
    printf("\nTest: Erase at size boundary\n");

    fesp_flash_ctx_t ctx;
    fesp_flash_init(&ctx, 4096);

    bool ok = fesp_flash_erase(&ctx, 4096, 1);
    TEST_ASSERT(!ok, "Erase at size boundary returns false");

    fesp_flash_close(&ctx);
}

/* ========================================================================
 * Main test runner
 * ======================================================================== */

int main(void)
{
    printf("=== Flash Storage Simulation Tests ===\n");

    test_init_and_close();
    test_init_zero_size();
    test_close_without_init();

    test_write_and_read();
    test_read_unwritten_is_ff();

    test_write_and_semantics();
    test_write_partial_overlap();

    test_erase_sector();
    test_erase_sector_alignment();
    test_erase_all();
    test_erase_all_without_init();

    test_calc_md5();
    test_calc_md5_invalid_region();

    test_read_out_of_bounds();
    test_write_out_of_bounds();
    test_erase_zero_length();
    test_erase_at_size_boundary();

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
