/*
 * chip.h - ESP chip characteristics (public API)
 */

#ifndef FESP_CHIP_H
#define FESP_CHIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FESP_CHIP_DETECT_REG 0x40001000
#define FESP_CHIP_NAME_MAX 32
#define FESP_SPI_REG_COUNT 64

#define FESP_FLASH_MODE_QIO 0
#define FESP_FLASH_MODE_DIO 2
#define FESP_FLASH_MODE_QOUT 1
#define FESP_FLASH_MODE_DOUT 3

#define FESP_FLASH_FREQ_40M 0
#define FESP_FLASH_FREQ_26M 1
#define FESP_FLASH_FREQ_20M 2
#define FESP_FLASH_FREQ_80M 3

#define FESP_XTAL_FREQ_40M 0
#define FESP_XTAL_FREQ_26M 1

typedef struct {
    uint8_t usr;
    uint8_t usr1;
    uint8_t usr2;
    uint8_t w0;
    uint8_t mosi_dlen;
    uint8_t miso_dlen;
} fesp_spi_offsets_t;

typedef enum {
    FESP_CHIP_ESP8266,
    FESP_CHIP_ESP32,
    FESP_CHIP_ESP32S2,
    FESP_CHIP_ESP32S3,
    FESP_CHIP_ESP32C2,
    FESP_CHIP_ESP32C3,
    FESP_CHIP_ESP32C6,
    FESP_CHIP_COUNT
} fesp_chip_type_t;

typedef struct fesp_chip_ctx_tag fesp_chip_ctx_t;

struct fesp_chip_ctx_tag {
    fesp_chip_type_t type;
    char name[FESP_CHIP_NAME_MAX];
    uint8_t mac[6];
    uint8_t *efuse;
    int efuse_size;
    uint32_t flash_size;
    uint32_t flash_id;
    uint8_t xtal_freq;
    uint32_t sector_size;
    uint32_t block_size;
    uint32_t page_size;
    uint32_t chip_id;
    uint32_t security_chip_id;
    uint32_t pkg_version;
    bool has_usb;
    uint32_t spi_reg_base;
    const fesp_spi_offsets_t *spi_offs;
    uint32_t spi_regs[FESP_SPI_REG_COUNT];
    uint32_t efuse_base;
    uint32_t pgm_data[32];
    uint32_t efuse_conf_ofs;
    uint32_t efuse_cmd_ofs;
};

bool fesp_chip_init(fesp_chip_ctx_t *ctx, fesp_chip_type_t type);
void fesp_chip_close(fesp_chip_ctx_t *ctx);
const char *fesp_chip_get_name(const fesp_chip_ctx_t *ctx);
bool fesp_chip_set_mac(fesp_chip_ctx_t *ctx, const uint8_t mac[6]);
const uint8_t *fesp_chip_get_mac(const fesp_chip_ctx_t *ctx);
uint32_t fesp_chip_read_reg(const fesp_chip_ctx_t *ctx, uint32_t addr);
bool fesp_chip_write_reg(fesp_chip_ctx_t *ctx, uint32_t addr, uint32_t val);
void fesp_chip_set_flash_size(fesp_chip_ctx_t *ctx, uint32_t size);
uint32_t fesp_chip_get_flash_size(const fesp_chip_ctx_t *ctx);
uint32_t fesp_chip_get_chip_id(const fesp_chip_ctx_t *ctx);
const uint8_t *fesp_chip_get_efuse(const fesp_chip_ctx_t *ctx);
uint8_t *fesp_chip_get_efuse_mut(fesp_chip_ctx_t *ctx);
int fesp_chip_get_efuse_size(const fesp_chip_ctx_t *ctx);
uint32_t fesp_chip_get_boot_baud_rate(const fesp_chip_ctx_t *ctx);
const char *fesp_chip_get_boot_message(const fesp_chip_ctx_t *ctx,
                                       bool download_mode, uint8_t reset_cause,
                                       char *buf, size_t buf_size);

#endif /* FESP_CHIP_H */
