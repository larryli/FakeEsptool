#include "chip.h"
#include "../utils/trace.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CHIP";

static void InitEsp8266(CHIP_CTX *ctx)
{
    strcpy(ctx->name, "ESP8266");
    ctx->chip_id = 0xFFF0C101;
    ctx->pkg_version = 0;
    ctx->efuse_size = 32;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = FALSE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 64;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = FALSE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 256;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = FALSE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
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
    ctx->efuse_size = 128;
    ctx->sector_size = 4096;
    ctx->block_size = 65536;
    ctx->page_size = 256;
    ctx->has_usb = TRUE;
    ctx->flash_id = 0x1640EF;

    ctx->efuse = (BYTE *)calloc(1, ctx->efuse_size);
    
    ctx->efuse[0] = 0x10;
    ctx->efuse[1] = 0x00;
    ctx->efuse[2] = 0x00;
    ctx->efuse[3] = 0x00;
}

BOOL Chip_Init(CHIP_CTX *ctx, CHIP_TYPE type)
{
    memset(ctx, 0, sizeof(CHIP_CTX));
    ctx->type = type;
    ctx->flash_size = 4 * 1024 * 1024;
    ctx->flash_mode = FLASH_MODE_DIO;
    ctx->flash_freq = FLASH_FREQ_40M;

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
    default:
        TRACE_FW(TAG, "Unknown chip type: %d", type);
        return FALSE;
    }

    TRACE_FW(TAG, "Chip: %s, eFuse: %d bytes, Flash: %lu KB", 
             ctx->name, ctx->efuse_size, ctx->flash_size / 1024);
    return TRUE;
}

void Chip_Close(CHIP_CTX *ctx)
{
    if (ctx->efuse) {
        free(ctx->efuse);
        ctx->efuse = NULL;
    }
}

const char *Chip_GetName(const CHIP_CTX *ctx)
{
    return ctx->name;
}

BOOL Chip_SetMac(CHIP_CTX *ctx, const BYTE mac[6])
{
    memcpy(ctx->mac, mac, 6);

    if (ctx->efuse && ctx->type == CHIP_ESP32 && ctx->efuse_size >= 24) {
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

DWORD Chip_ReadReg(const CHIP_CTX *ctx, DWORD addr)
{
    if (addr >= 0x3FF00000 && addr < 0x3FF00000 + ctx->efuse_size) {
        int offset = (int)(addr - 0x3FF00000);
        if (offset + 3 < ctx->efuse_size) {
            return ctx->efuse[offset] | 
                   ((DWORD)ctx->efuse[offset + 1] << 8) |
                   ((DWORD)ctx->efuse[offset + 2] << 16) | 
                   ((DWORD)ctx->efuse[offset + 3] << 24);
        }
    }

    if (addr == 0x3FF5A000)
        return ctx->chip_id;

    if (addr == 0x3F400010)
        return (ctx->flash_size >> 16) & 0xFFFF;

    return 0;
}

BOOL Chip_WriteReg(CHIP_CTX *ctx, DWORD addr, DWORD val)
{
    if (addr >= 0x3FF00000 && addr < 0x3FF00000 + ctx->efuse_size) {
        int offset = (int)(addr - 0x3FF00000);
        if (offset + 3 < ctx->efuse_size) {
            ctx->efuse[offset] |= (BYTE)(val & 0xFF);
            ctx->efuse[offset + 1] |= (BYTE)((val >> 8) & 0xFF);
            ctx->efuse[offset + 2] |= (BYTE)((val >> 16) & 0xFF);
            ctx->efuse[offset + 3] |= (BYTE)((val >> 24) & 0xFF);
            TRACE_FW(TAG, "eFuse write: offset=0x%X val=0x%08lX", offset, val);
        }
    }
    return TRUE;
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

const BYTE *Chip_GetEfuse(const CHIP_CTX *ctx)
{
    return ctx->efuse;
}

int Chip_GetEfuseSize(const CHIP_CTX *ctx)
{
    return ctx->efuse_size;
}
