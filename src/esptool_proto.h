/*
 * esptool_proto.h - Protocol adapter layer
 *
 * Bridges serial.c callbacks to esptool protocol processing.
 */

#ifndef ESPROTO_H
#define ESPROTO_H

#include "serial.h"
#include "esptool/esptool.h"

extern ESPTOOL_CTX g_esptool;

/* Initialize protocol layer */
void EsptoolProto_Init(void);

/* Data receive callback - called from serial listener thread */
void EsptoolProto_ProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify);

/* Signal change callback - called when DSR/CTS signals change */
void EsptoolProto_OnSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify);

/* Set chip type for new device */
void EsptoolProto_SetChipType(CHIP_TYPE type);

/* Set flash size for new device */
void EsptoolProto_SetFlashSize(DWORD size);

/* Set callback for device data modification */
void EsptoolProto_SetModifiedCallback(ESP_MODIFIED_CB cb);

#endif
