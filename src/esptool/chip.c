#include "chip.h"
#include "../utils/trace.h"
#include <string.h>

static const char *TAG = "CHIP";

static void InitEsp8266(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP8266");
    ctx->chip_id = 0xFFF0C101;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x01;
    ctx->efuse[1] = 0xC1;
    ctx->efuse[2] = 0xF0;
    ctx->efuse[3] = 0xFF;
}

static void InitEsp32(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32");
    ctx->chip_id = 0x000F0106;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x06;
    ctx->efuse[1] = 0xF0;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;

    ctx->efuse[16] = ctx->mac[0];
    ctx->efuse[17] = ctx->mac[1];
    ctx->efuse[18] = ctx->mac[2];
    ctx->efuse[19] = ctx->mac[3];
    ctx->efuse[20] = ctx->mac[4];
    ctx->efuse[21] = ctx->mac[5];
}

static void InitEsp32S2(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-S2");
    ctx->chip_id = 0x000007C6;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0xC6;
    ctx->efuse[1] = 0x07;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

static void InitEsp32S3(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-S3");
    ctx->chip_id = 0x00000009;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x09;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

static void InitEsp32C2(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-C2");
    ctx->chip_id = 0x0000000C;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x0C;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

static void InitEsp32C3(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-C3");
    ctx->chip_id = 0x00000005;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x05;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

static void InitEsp32C6(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-C6");
    ctx->chip_id = 0x0000000D;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x0D;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

static void InitEsp32C61(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-C61");
    ctx->chip_id = 0x0000000E;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x0E;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

static void InitEsp32H2(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP32-H2");
    ctx->chip_id = 0x00000010;
    ctx->pkg_version = 0;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->eeprom_size = 0;

    ctx->efuse[0] = 0x10;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

void Chip_Init(CHIP_CTX *ctx, CHIP_TYPE type)
{
    memset(ctx, 0, sizeof(CHIP_CTX));
    ctx->type = type;
    ctx->flash_size = 4 * 1024 * 1024;

    ctx->mac[0] = 0xAA;
    ctx->mac[1] = 0xBB;
    ctx->mac[2] = 0xCC;
    ctx->mac[3] = 0xDD;
    ctx->mac[4] = 0xEE;
    ctx->mac[5] = 0x01;

    switch (type) {
    case CHIP_ESP8266: InitEsp8266(ctx); break;
    case CHIP_ESP32:   InitEsp32(ctx);   break;
    case CHIP_ESP32S2: InitEsp32S2(ctx); break;
    case CHIP_ESP32S3: InitEsp32S3(ctx); break;
    case CHIP_ESP32C2: InitEsp32C2(ctx); break;
    case CHIP_ESP32C3: InitEsp32C3(ctx); break;
    case CHIP_ESP32C6: InitEsp32C6(ctx); break;
    case CHIP_ESP32C61: InitEsp32C61(ctx); break;
    case CHIP_ESP32H2: InitEsp32H2(ctx); break;
    default:           InitEsp32(ctx);   break;
    }

    TRACE_FW(TAG, "Chip: %s, Flash: %lu KB", ctx->name, ctx->flash_size / 1024);
}

const char *Chip_GetName(const CHIP_CTX *ctx)
{
    return ctx->name;
}

BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6])
{
    memcpy(ctx->mac, mac, 6);

    if (ctx->type == CHIP_ESP32) {
        ctx->efuse[16] = mac[0];
        ctx->efuse[17] = mac[1];
        ctx->efuse[18] = mac[2];
        ctx->efuse[19] = mac[3];
        ctx->efuse[20] = mac[4];
        ctx->efuse[21] = mac[5];
    }

    return TRUE;
}

const BYTE *Chip_GetMac(const CHIP_CTX *ctx)
{
    return ctx->mac;
}

void Chip_GetEfuseBlock(const CHIP_CTX *ctx, int block, BYTE data[32])
{
    int offset = block * 32;
    if (offset + 32 > EFUSE_SIZE) {
        memset(data, 0, 32);
        return;
    }
    memcpy(data, &ctx->efuse[offset], 32);
}

void Chip_SetFlashSize(CHIP_CTX *ctx, DWORD size)
{
    ctx->flash_size = size;
}

DWORD Chip_GetFlashSize(const CHIP_CTX *ctx)
{
    return ctx->flash_size;
}

DWORD Chip_GetChipId(const CHIP_CTX *ctx)
{
    return ctx->chip_id;
}
