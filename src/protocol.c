/*
 * protocol.c - Protocol handler implementation
 *
 * ECHO protocol: received data is sent back as-is (loopback).
 * Ping function: sends random data for testing.
 *
 * ============================================================
 * Extension Guide
 * ============================================================
 *
 * To implement your protocol:
 *
 * 1. Create your callback function:
 *
 *    void MyProtocol_ProcessData(SERIAL_CTX *ctx, const BYTE *data,
 *                                 DWORD len, HWND hNotify)
 *    {
 *        if (data[0] == 0x01) {
 *            BYTE resp[] = {0x01, GetSensorValue()};
 *            Serial_WriteData(ctx, resp, sizeof(resp), hNotify);
 *            Serial_PostLogF(hNotify, L"MY", L"Query: value=%d", GetSensorValue());
 *        }
 *    }
 *
 * 2. Register your callback in Main_OnConnect:
 *
 *    Serial_SetReceiveCallback(&g_serial, (SERIAL_RX_CB)MyProtocol_ProcessData);
 *
 * ============================================================
 */

#include "protocol.h"
#include "utils/trace.h"
#include <stdlib.h>

static const char *TAG = "PROTO";

#define PING_MIN_SIZE   1
#define PING_MAX_SIZE   256

void Protocol_Init(void)
{
    srand((unsigned int)GetTickCount64());
    TRACE_PROTO(TAG, "Protocol module initialized");
}

/*
 * Protocol_ProcessData - ECHO protocol handler
 *
 * Receives data and sends it back unchanged (loopback).
 * This is the default callback registered via Serial_SetReceiveCallback().
 */
void Protocol_ProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify)
{
    if (!ctx || !data || len == 0)
        return;

    Serial_PostLogF(hNotify, L"ECHO", L"Received %lu bytes", len);

    /* Allocate buffer for response */
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, len);
    if (!buf)
        return;

    /* Copy and send back */
    CopyMemory(buf, data, len);
    Serial_WriteData(ctx, buf, len, hNotify);
    HeapFree(GetProcessHeap(), 0, buf);

    Serial_PostLogF(hNotify, L"ECHO", L"Send %lu bytes", len);
}

/*
 * Protocol_SendPing - Send random data for testing
 *
 * Generates and sends 1-256 bytes of random data.
 * Useful for testing serial connection and throughput.
 */
void Protocol_SendPing(SERIAL_CTX *ctx, HWND hNotify)
{
    if (!ctx)
        return;

    DWORD size = PING_MIN_SIZE + (rand() % (PING_MAX_SIZE - PING_MIN_SIZE + 1));

    BYTE *data = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!data)
        return;

    for (DWORD i = 0; i < size; i++)
        data[i] = (BYTE)(rand() % 256);

    Serial_WriteData(ctx, data, size, hNotify);
    HeapFree(GetProcessHeap(), 0, data);

    Serial_PostLogF(hNotify, L"PING", L"Sent %lu random bytes", size);
}
