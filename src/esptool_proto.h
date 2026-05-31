#ifndef ESPROTO_H
#define ESPROTO_H

#include "serial.h"
#include "esptool/esptool.h"

extern ESPTOOL_CTX g_esptool;

void EsptoolProto_Init(void);
void EsptoolProto_ProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify);
void EsptoolProto_OnSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify);

void EsptoolProto_SetChipType(CHIP_TYPE type);
void EsptoolProto_SetFlashSize(DWORD size);
void EsptoolProto_SetModifiedCallback(ESP_MODIFIED_CB cb);

#endif
