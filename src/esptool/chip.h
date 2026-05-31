#ifndef ESP_CHIP_H
#define ESP_CHIP_H

#include <windows.h>

#define CHIP_NAME_MAX   32

#define FLASH_MODE_QIO  0
#define FLASH_MODE_DIO  2
#define FLASH_MODE_QOUT 1
#define FLASH_MODE_DOUT 3

#define FLASH_FREQ_40M  0
#define FLASH_FREQ_26M  1
#define FLASH_FREQ_20M  2
#define FLASH_FREQ_80M  0x0F

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
    
    BYTE *efuse;
    int efuse_size;
    
    DWORD flash_size;
    DWORD flash_id;
    BYTE flash_mode;
    BYTE flash_freq;
    
    DWORD sector_size;
    DWORD block_size;
    DWORD page_size;
    
    DWORD chip_id;
    DWORD pkg_version;
    BOOL has_usb;
} CHIP_CTX;

BOOL Chip_Init(CHIP_CTX *ctx, CHIP_TYPE type);
void Chip_Close(CHIP_CTX *ctx);
const char *Chip_GetName(const CHIP_CTX *ctx);
BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6]);
const BYTE *Chip_GetMac(const CHIP_CTX *ctx);
DWORD Chip_ReadReg(const CHIP_CTX *ctx, DWORD addr);
BOOL Chip_WriteReg(CHIP_CTX *ctx, DWORD addr, DWORD val);
void Chip_SetFlashSize(CHIP_CTX *ctx, DWORD size);
DWORD Chip_GetFlashSize(const CHIP_CTX *ctx);
DWORD Chip_GetChipId(const CHIP_CTX *ctx);
const BYTE *Chip_GetEfuse(const CHIP_CTX *ctx);
int Chip_GetEfuseSize(const CHIP_CTX *ctx);

#endif
