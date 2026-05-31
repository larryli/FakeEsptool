/*
 * esptool_proto.c - Protocol adapter layer implementation
 *
 * Bridges serial.c callbacks to esptool protocol processing.
 */

#include "esptool_proto.h"
#include "utils/trace.h"

#if ENABLE_TRACE
static const char *TAG = "ESPPROTO";
#endif

ESPTOOL_CTX g_esptool;

void EsptoolProto_Init(void)
{
    Esptool_Init(&g_esptool);
    TRACE_PROTO(TAG, "Esptool protocol initialized");
}

void EsptoolProto_ProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify)
{
    if (!ctx || !data || len == 0)
        return;

    g_esptool.hNotify = hNotify;
    Esptool_Feed(&g_esptool, data, (int)len);
}

void EsptoolProto_OnSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify)
{
    (void)ctx;
    (void)hNotify;

#if ENABLE_TRACE
    BOOL dsr = (modemStatus & MS_DSR_ON) != 0;
    BOOL cts = (modemStatus & MS_CTS_ON) != 0;
    TRACE_PROTO(TAG, "Signal: DSR=%d CTS=%d", dsr, cts);
#endif
}

void EsptoolProto_SetChipType(CHIP_TYPE type)
{
    Esptool_SetChipType(&g_esptool, type);
}

void EsptoolProto_SetFlashSize(DWORD size)
{
    Esptool_SetFlashSize(&g_esptool, size);
}

void EsptoolProto_SetModifiedCallback(ESP_MODIFIED_CB cb)
{
    Esptool_SetModifiedCallback(&g_esptool, cb);
}
