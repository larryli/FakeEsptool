/*
 * flash.h - Flash storage simulation (public API)
 */

#ifndef FESP_FLASH_H
#define FESP_FLASH_H

#include <stdint.h>
#include <stdbool.h>

#define FESP_FLASH_ERASE_PATTERN 0xFF
#define FESP_FLASH_SECTOR_SIZE 4096

typedef struct {
    uint8_t *data;
    uint32_t size;
} fesp_flash_ctx_t;

bool fesp_flash_init(fesp_flash_ctx_t *ctx, uint32_t size);
void fesp_flash_close(fesp_flash_ctx_t *ctx);
bool fesp_flash_read(const fesp_flash_ctx_t *ctx, uint32_t addr, uint8_t *buf, uint32_t len);
bool fesp_flash_write(fesp_flash_ctx_t *ctx, uint32_t addr, const uint8_t *data, uint32_t len);
bool fesp_flash_erase(fesp_flash_ctx_t *ctx, uint32_t addr, uint32_t len);
bool fesp_flash_erase_all(fesp_flash_ctx_t *ctx);
void fesp_flash_calc_md5(const fesp_flash_ctx_t *ctx, uint32_t addr, uint32_t len, uint8_t md5[16]);

#endif /* FESP_FLASH_H */
