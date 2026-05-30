/*
 * protocol.h - Protocol handler interface
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <windows.h>
#include "serial.h"

/*
 * Protocol_Init - Initialize protocol module
 */
void Protocol_Init(void);

/*
 * Protocol_ProcessData - ECHO callback: send received data back
 *
 * Matches SERIAL_RX_CB signature (registered via Serial_SetReceiveCallback).
 *
 * @ctx: Serial context (SERIAL_CTX*)
 * @data: Received data
 * @len: Length of received data
 * @hNotify: Window for UI notifications
 */
void Protocol_ProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify);

/*
 * Protocol_SendPing - Send random test data (1-256 bytes)
 */
void Protocol_SendPing(SERIAL_CTX *ctx, HWND hNotify);

#endif /* PROTOCOL_H */
