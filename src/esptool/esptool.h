#ifndef ESP_ESPTOOL_H
#define ESP_ESPTOOL_H

#include <windows.h>
#include "slip.h"
#include "chip.h"
#include "flash.h"

#ifndef WM_SERIAL_TX
#define WM_SERIAL_TX (WM_USER + 2)
#endif

#define ESP_DIR_REQUEST     0x00
#define ESP_DIR_RESPONSE    0x01

#define ESP_OK              0x00
#define ESP_FAIL            0x01

#define ESP_CMD_SYNC            0x08
#define ESP_CMD_SPI_SET_PARAMS  0x09
#define ESP_CMD_READ_REG        0x0A
#define ESP_CMD_WRITE_REG       0x0B
#define ESP_CMD_SPI_ATTACH      0x0D
#define ESP_CMD_CHANGE_BAUDRATE 0x0F
#define ESP_CMD_SPI_FLASH_MD5   0x13
#define ESP_CMD_SPI_FLASH_DATA  0x14
#define ESP_CMD_SPI_READ_FLASH  0x15
#define ESP_CMD_SPI_ERASE_FLASH 0x16
#define ESP_CMD_SPI_ERASE_BLOCK 0x17
#define ESP_CMD_SPI_FLASH_BLOCK 0x18
#define ESP_CMD_READ_FLASH_SFDP 0x1A
#define ESP_CMD_SPI_FLASH_DEFL_BEGIN  0x20
#define ESP_CMD_SPI_FLASH_DEFL_DATA   0x21
#define ESP_CMD_SPI_FLASH_DEFL_END    0x22
#define ESP_CMD_SPI_FLASH_DEFL_MD5    0x23

#define ESP_CMD_READ_FLASH      0xE0
#define ESP_CMD_WRITE_FLASH     0xE1

#define ESP_CMD_MEM_BEGIN       0x05
#define ESP_CMD_MEM_END         0x06
#define ESP_CMD_MEM_DATA        0x07

#define ESP_SYNC_SEQ_LEN    36

#define ESP_FLASH_BLOCK_SIZE    0x1000
#define ESP_FLASH_ERASE_SIZE    0x10000

typedef struct {
    SLIP_CTX  slip;
    CHIP_CTX  chip;
    FLASH_CTX flash;
    BOOL      synced;
    HWND      hNotify;
} ESPTOOL_CTX;

typedef struct {
    BYTE  direction;
    BYTE  command;
    WORD  size;
    DWORD value;
    BYTE  data[2048];
} ESP_PACKET;

void Esptool_Init(ESPTOOL_CTX *ctx);
void Esptool_SetNotify(ESPTOOL_CTX *ctx, HWND hNotify);
void Esptool_SetChipType(ESPTOOL_CTX *ctx, CHIP_TYPE type);
void Esptool_SetFlashSize(ESPTOOL_CTX *ctx, DWORD size);
BOOL Esptool_Feed(ESPTOOL_CTX *ctx, const BYTE *data, int len);
BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len);
void Esptool_SendResponse(ESPTOOL_CTX *ctx, BYTE cmd, DWORD status, const BYTE *data, WORD data_len);
BYTE Esptool_CalcChecksum(const BYTE *data, int len);

#endif
