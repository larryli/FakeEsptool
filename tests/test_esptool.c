/*
 * test_esptool.c - Unit tests for esptool protocol handler
 *
 * Tests init/close, checksum, SLIP frame processing, and basic commands.
 */

#include "../src/fesptool/fesp.h"
#include "hal_test.h"
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

/* Helper: build a SLIP frame from raw packet data */
static int build_frame(const uint8_t *pkt, int pkt_len, uint8_t *out,
                       int out_max)
{
    return fesp_slip_encode(pkt, pkt_len, out, out_max);
}

/* Helper: build a request packet (direction=0x00) */
static int build_request(uint8_t cmd, uint32_t value, const uint8_t *data,
                         uint16_t data_len, uint8_t *pkt, int pkt_max)
{
    if (8 + data_len > pkt_max)
        return -1;

    pkt[0] = 0x00; /* direction = request */
    pkt[1] = cmd;
    pkt[2] = (uint8_t)(data_len & 0xFF);
    pkt[3] = (uint8_t)((data_len >> 8) & 0xFF);
    pkt[4] = (uint8_t)(value & 0xFF);
    pkt[5] = (uint8_t)((value >> 8) & 0xFF);
    pkt[6] = (uint8_t)((value >> 16) & 0xFF);
    pkt[7] = (uint8_t)((value >> 24) & 0xFF);

    if (data_len > 0 && data)
        memcpy(pkt + 8, data, data_len);

    /* Append checksum */
    uint8_t checksum = 0;
    for (int i = 0; i < 8 + data_len; i++)
        checksum ^= pkt[i];
    pkt[8 + data_len] = checksum;

    return 8 + data_len + 1;
}

/* Helper: feed a request through the protocol stack */
static void feed_request(fesp_ctx_t *ctx, uint8_t cmd, uint32_t value,
                         const uint8_t *data, uint16_t data_len)
{
    uint8_t pkt[4096];
    int pkt_len = build_request(cmd, value, data, data_len, pkt, sizeof(pkt));
    TEST_ASSERT(pkt_len > 0, "Packet built");

    uint8_t frame[8192];
    int frame_len = build_frame(pkt, pkt_len, frame, sizeof(frame));
    TEST_ASSERT(frame_len > 0, "Frame encoded");

    fesp_feed(ctx, frame, frame_len);
}

/* ========================================================================
 * Init / Close lifecycle
 * ======================================================================== */

static void test_init_close(void)
{
    printf("\nTest: Init and close\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;

    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);

    TEST_ASSERT(ctx.state == FESP_STATE_IDLE, "Initial state is IDLE");
    TEST_ASSERT(!ctx.synced, "Not synced");
    TEST_ASSERT(!ctx.stub_mode, "Not in stub mode");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);

    TEST_ASSERT(true, "Close does not crash");
}

/* ========================================================================
 * Checksum
 * ======================================================================== */

static void test_checksum(void)
{
    printf("\nTest: Checksum calculation\n");

    /* Checksum starts at 0xEF, then XOR each byte */
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t cs = fesp_calc_checksum(data, 3);
    /* 0xEF ^ 0x01 ^ 0x02 ^ 0x03 = 0xEF */
    TEST_ASSERT(cs == 0xEF, "XOR checksum correct");

    uint8_t data2[] = {0xFF, 0x00, 0xFF};
    cs = fesp_calc_checksum(data2, 3);
    /* 0xEF ^ 0xFF ^ 0x00 ^ 0xFF = 0xEF */
    TEST_ASSERT(cs == 0xEF, "Checksum of 0xFF,0x00,0xFF is 0xEF");

    uint8_t data3[] = {0x00};
    cs = fesp_calc_checksum(data3, 1);
    TEST_ASSERT(cs == (0xEF ^ 0x00), "Single byte checksum");
}

/* ========================================================================
 * SYNC command
 * ======================================================================== */

static void test_sync_command(void)
{
    printf("\nTest: SYNC command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    feed_request(&ctx, FESP_CMD_SYNC, 0x08000000, NULL, 0);

    /* SYNC should produce FESP_SYNC_RESPONSE_COUNT (8) response frames */
    TEST_ASSERT(stub_get_resp_len() > 0, "SYNC produces response");
    TEST_ASSERT(ctx.synced, "SYNC sets synced flag");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * READ_REG command
 * ======================================================================== */

static void test_read_reg_command(void)
{
    printf("\nTest: READ_REG command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    /* READ_REG: address goes in data field (4 bytes LE) */
    uint32_t addr = 0x40001000;
    uint8_t param[4];
    memcpy(param, &addr, 4);
    feed_request(&ctx, FESP_CMD_READ_REG, 0, param, 4);

    TEST_ASSERT(stub_get_resp_len() > 0, "READ_REG produces response");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * WRITE_REG command
 * ======================================================================== */

static void test_write_reg_command(void)
{
    printf("\nTest: WRITE_REG command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32S3);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    /* WRITE_REG: [addr:4][value:4][mask:4][delay:4] */
    uint32_t addr = 0x60002004;
    uint32_t val = 0xDEADBEEF;
    uint32_t mask = 0xFFFFFFFF;
    uint32_t delay = 0;
    uint8_t param[16];
    memcpy(param + 0, &addr, 4);
    memcpy(param + 4, &val, 4);
    memcpy(param + 8, &mask, 4);
    memcpy(param + 12, &delay, 4);
    feed_request(&ctx, FESP_CMD_WRITE_REG, 0, param, 16);

    TEST_ASSERT(stub_get_resp_len() > 0, "WRITE_REG produces response");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * CHANGE_BAUDRATE command
 * ======================================================================== */

static void test_change_baudrate(void)
{
    printf("\nTest: CHANGE_BAUDRATE command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    /* First sync to enter READY state */
    feed_request(&ctx, FESP_CMD_SYNC, 0x08000000, NULL, 0);
    stub_reset();

    /* CHANGE_BAUDRATE: data = new_baud (4 bytes LE) */
    uint32_t new_baud = 460800;
    uint8_t param[4];
    memcpy(param, &new_baud, 4);

    feed_request(&ctx, FESP_CMD_CHANGE_BAUDRATE, 0, param, 4);

    TEST_ASSERT(stub_get_resp_len() > 0, "CHANGE_BAUDRATE produces response");
    TEST_ASSERT(stub_get_baud_changed(), "Baud rate change callback called");
    TEST_ASSERT(stub_get_baud_rate() == 460800, "Baud rate is 460800");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * SPI_SET_PARAMS command
 * ======================================================================== */

static void test_spi_set_params(void)
{
    printf("\nTest: SPI_SET_PARAMS command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    feed_request(&ctx, FESP_CMD_SPI_SET_PARAMS, 0, NULL, 0);

    TEST_ASSERT(stub_get_resp_len() > 0, "SPI_SET_PARAMS produces response");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * SPI_ATTACH command
 * ======================================================================== */

static void test_spi_attach(void)
{
    printf("\nTest: SPI_ATTACH command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    feed_request(&ctx, FESP_CMD_SPI_ATTACH, 0, NULL, 0);

    TEST_ASSERT(stub_get_resp_len() > 0, "SPI_ATTACH produces response");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * SPI_FLASH_MD5 command
 * ======================================================================== */

static void test_spi_flash_md5(void)
{
    printf("\nTest: SPI_FLASH_MD5 command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    /* MD5 of first 16 bytes at offset 0 */
    uint8_t md5_data[16];
    uint32_t offset = 0;
    uint32_t size = 16;
    uint8_t param[12];
    memcpy(param + 0, &offset, 4);
    memcpy(param + 4, &size, 4);
    memcpy(param + 8, md5_data, 4); /* first 4 bytes of expected MD5 */

    feed_request(&ctx, FESP_CMD_SPI_FLASH_MD5, 0, param, 12);

    TEST_ASSERT(stub_get_resp_len() > 0, "SPI_FLASH_MD5 produces response");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * GET_SECURITY_INFO command
 * ======================================================================== */

static void test_get_security_info(void)
{
    printf("\nTest: GET_SECURITY_INFO command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    feed_request(&ctx, FESP_CMD_GET_SECURITY_INFO, 0, NULL, 0);

    TEST_ASSERT(stub_get_resp_len() > 0,
                "GET_SECURITY_INFO produces response");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * ERASE_FLASH command
 * ======================================================================== */

static void test_erase_flash(void)
{
    printf("\nTest: ERASE_FLASH command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);

    /* SYNC + READ_REG to reach READY state */
    feed_request(&ctx, FESP_CMD_SYNC, 0x08000000, NULL, 0);
    uint32_t detect_addr = 0x40001000;
    uint8_t detect_param[4];
    memcpy(detect_param, &detect_addr, 4);
    feed_request(&ctx, FESP_CMD_READ_REG, 0, detect_param, 4);
    stub_reset();

    feed_request(&ctx, FESP_CMD_ERASE_FLASH, 0, NULL, 0);

    TEST_ASSERT(stub_get_resp_len() > 0, "ERASE_FLASH produces response");
    TEST_ASSERT(stub_get_modified(), "Modified callback called");

    /* Flash should be all 0xFF after erase */
    uint8_t rbuf[4] = {0};
    fesp_flash_read(&flash, 0, rbuf, 4);
    bool all_ff = (rbuf[0] == 0xFF && rbuf[1] == 0xFF &&
                   rbuf[2] == 0xFF && rbuf[3] == 0xFF);
    TEST_ASSERT(all_ff, "Flash erased to 0xFF");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * ERASE_REGION command
 * ======================================================================== */

static void test_erase_region(void)
{
    printf("\nTest: ERASE_REGION command\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 8192);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    /* Write data at offset 4096 (sector 1) */
    uint8_t wdata[4] = {0x00, 0x00, 0x00, 0x00};
    fesp_flash_write(&flash, 4096, wdata, 4);

    /* ERASE_REGION: offset=4096, size=4096 */
    uint8_t param[8];
    uint32_t offset = 4096;
    uint32_t size = 4096;
    memcpy(param + 0, &offset, 4);
    memcpy(param + 4, &size, 4);

    feed_request(&ctx, FESP_CMD_ERASE_REGION, 0, param, 8);

    TEST_ASSERT(stub_get_resp_len() > 0, "ERASE_REGION produces response");

    /* Sector 0 should be untouched */
    uint8_t rbuf[1];
    fesp_flash_read(&flash, 0, rbuf, 1);
    TEST_ASSERT(rbuf[0] == 0xFF, "Sector 0 still 0xFF");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * Multiple sequential commands
 * ======================================================================== */

static void test_sequential_commands(void)
{
    printf("\nTest: Sequential commands\n");

    fesp_chip_ctx_t chip;
    fesp_flash_ctx_t flash;
    fesp_ctx_t ctx;
    fesp_chip_init(&chip, FESP_CHIP_ESP32);
    fesp_flash_init(&flash, 4096);
    fesp_init(&ctx, &chip, &flash);
    stub_reset();

    /* SYNC */
    feed_request(&ctx, FESP_CMD_SYNC, 0x08000000, NULL, 0);
    TEST_ASSERT(ctx.synced, "After SYNC: synced");

    /* READ_REG should produce response even in SYNCED state */
    stub_reset();
    uint32_t detect_addr = 0x40001000;
    uint8_t detect_param[4];
    memcpy(detect_param, &detect_addr, 4);
    feed_request(&ctx, FESP_CMD_READ_REG, 0, detect_param, 4);
    int read_resp = stub_get_resp_len();
    TEST_ASSERT(read_resp > 0, "After READ_REG: response produced");

    /* SPI_SET_PARAMS should produce response after READ_REG transitions to
     * READY */
    stub_reset();
    feed_request(&ctx, FESP_CMD_SPI_SET_PARAMS, 0, NULL, 0);
    TEST_ASSERT(stub_get_resp_len() > 0, "After SPI_SET_PARAMS: response");

    fesp_close(&ctx);
    fesp_flash_close(&flash);
    fesp_chip_close(&chip);
}

/* ========================================================================
 * Main test runner
 * ======================================================================== */

int main(void)
{
    printf("=== esptool Protocol Handler Tests ===\n");

    test_init_close();
    test_checksum();

    test_sync_command();
    test_read_reg_command();
    test_write_reg_command();
    test_change_baudrate();
    test_spi_set_params();
    test_spi_attach();
    test_spi_flash_md5();
    test_get_security_info();
    test_erase_flash();
    test_erase_region();
    test_sequential_commands();

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
