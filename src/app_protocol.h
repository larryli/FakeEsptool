/*
 * app_protocol.h - Protocol signal state machine
 *
 * Detects DTR/RTS signal sequences to determine download mode vs normal boot.
 */

#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include "serial.h"
#include <windows.h>

/*
 * OnEsptoolProcessData - Receive serial data and feed to esptool protocol
 */
void OnEsptoolProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len,
                          HWND hNotify);

/*
 * OnEsptoolSignal - Detect ClassicReset/HardReset via DTR/RTS signals
 */
void OnEsptoolSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify);

/*
 * ResetSignalState - Reset signal detection state (call on connect)
 */
void ResetSignalState(void);

#endif /* APP_PROTOCOL_H */
