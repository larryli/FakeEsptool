#include "esptool_proto.h"
#include "utils/trace.h"

static const char *TAG = "ESPPROTO";

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

    BOOL dsr = (modemStatus & MS_DSR_ON) != 0;
    BOOL cts = (modemStatus & MS_CTS_ON) != 0;

    TRACE_PROTO(TAG, "Signal: DSR=%d CTS=%d", dsr, cts);
}

void EsptoolProto_SetChipType(CHIP_TYPE type)
{
    Esptool_SetChipType(&g_esptool, type);
}

void EsptoolProto_SetFlashSize(DWORD size)
{
    Esptool_SetFlashSize(&g_esptool, size);
}
