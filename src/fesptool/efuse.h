/*
 * efuse.h - eFuse field definitions and constants (public API)
 */

#ifndef FESP_EFUSE_H
#define FESP_EFUSE_H

#include "chip.h"

#define FESP_KEY_PURPOSE_USER 0
#define FESP_KEY_PURPOSE_RESERVED 1
#define FESP_KEY_PURPOSE_XTS_AES_256_KEY_1 2
#define FESP_KEY_PURPOSE_XTS_AES_256_KEY_2 3
#define FESP_KEY_PURPOSE_XTS_AES_128_KEY 4
#define FESP_KEY_PURPOSE_HMAC_DOWN_ALL 5
#define FESP_KEY_PURPOSE_HMAC_DOWN_JTAG 6
#define FESP_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE 7
#define FESP_KEY_PURPOSE_HMAC_UP 8
#define FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST0 9
#define FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST1 10
#define FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST2 11

uint32_t fesp_efuse_get_flash_crypt_cnt(const fesp_chip_ctx_t *ctx);
bool fesp_efuse_is_flash_encryption_enabled(const fesp_chip_ctx_t *ctx);
bool fesp_efuse_is_download_encrypt_disabled(const fesp_chip_ctx_t *ctx);
bool fesp_efuse_is_download_decrypt_disabled(const fesp_chip_ctx_t *ctx);
bool fesp_efuse_is_download_mode_disabled(const fesp_chip_ctx_t *ctx);
bool fesp_efuse_is_secure_download_enabled(const fesp_chip_ctx_t *ctx);
uint32_t fesp_efuse_get_dl_encrypt_disabled(const fesp_chip_ctx_t *ctx);
uint32_t fesp_efuse_get_dl_mode_disabled(const fesp_chip_ctx_t *ctx);
uint32_t fesp_efuse_get_secure_boot_flag(const fesp_chip_ctx_t *ctx);
uint32_t fesp_efuse_get_jtag_flag(const fesp_chip_ctx_t *ctx);
bool fesp_efuse_is_secure_boot_enabled(const fesp_chip_ctx_t *ctx);
bool fesp_efuse_is_jtag_disabled(const fesp_chip_ctx_t *ctx);
int fesp_efuse_get_jtag_disabled_count(const fesp_chip_ctx_t *ctx);
int fesp_efuse_get_jtag_total_count(const fesp_chip_ctx_t *ctx);
uint32_t fesp_efuse_get_soft_jtag_flag(const fesp_chip_ctx_t *ctx);
uint32_t fesp_efuse_get_usb_jtag_flag(const fesp_chip_ctx_t *ctx);
uint8_t fesp_efuse_get_key_purpose(const fesp_chip_ctx_t *ctx, int block);
void fesp_efuse_set_key_purpose(fesp_chip_ctx_t *ctx, int block, uint8_t purpose);
int fesp_efuse_get_encryption_key_offset(const fesp_chip_ctx_t *ctx, int *key_len);
void fesp_efuse_set_flash_encryption(fesp_chip_ctx_t *ctx, int mode);
void fesp_efuse_set_download_mode(fesp_chip_ctx_t *ctx, int mode);
void fesp_efuse_apply_block0_defaults(fesp_chip_ctx_t *ctx);

#endif /* FESP_EFUSE_H */
