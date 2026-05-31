#ifndef ESP_CHIP_H
#define ESP_CHIP_H

#include <windows.h>

#define CHIP_NAME_MAX   32
#define EFUSE_SIZE      256

typedef enum {
    CHIP_ESP8266,
    CHIP_ESP32,
    CHIP_ESP32S2,
    CHIP_ESP32S3,
    CHIP_ESP32C2,
    CHIP_ESP32C3,
    CHIP_ESP32C6,
    CHIP_ESP32C61,
    CHIP_ESP32H2,
    CHIP_COUNT
} CHIP_TYPE;

typedef struct {
    CHIP_TYPE type;
    char name[CHIP_NAME_MAX];
    BYTE mac[6];
    BYTE efuse[EFUSE_SIZE];
    DWORD flash_size;
    DWORD block_size;
    DWORD page_size;
    DWORD eeprom_size;
    DWORD chip_id;
    DWORD pkg_version;
} CHIP_CTX;

void Chip_Init(CHIP_CTX *ctx, CHIP_TYPE type);
const char *Chip_GetName(const CHIP_CTX *ctx);
BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6]);
const BYTE *Chip_GetMac(const CHIP_CTX *ctx);
void Chip_GetEfuseBlock(const CHIP_CTX *ctx, int block, BYTE data[32]);
void Chip_SetFlashSize(CHIP_CTX *ctx, DWORD size);
DWORD Chip_GetFlashSize(const CHIP_CTX *ctx);
DWORD Chip_GetChipId(const CHIP_CTX *ctx);

#endif
