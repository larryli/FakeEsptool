/*
 * serial.h - Serial port communication module interface
 *
 * Provides functions for enumerating, opening, closing serial ports,
 * reading/writing data, and signal control using WaitCommEvent-based
 * event-driven I/O.
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <windows.h>

/* Data direction for log display */
typedef enum { DIR_RX, DIR_TX } DATA_DIR;

/* Receive callback type - called when data is received */
typedef void (*SERIAL_RX_CB)(void *ctx, const BYTE *data, DWORD len, HWND hNotify);

/* Signal change callback type - called when DSR/CTS signals change
 * @modemStatus: Result of GetCommModemStatus() (MS_DSR_ON, MS_CTS_ON, etc.)
 */
typedef void (*SERIAL_SIGNAL_CB)(void *ctx, DWORD modemStatus, HWND hNotify);

/* Serial port context */
typedef struct {
    HANDLE hPort;           /* Serial port handle */
    HANDLE hThread;         /* Listener thread handle */
    HANDLE hStartEvent;     /* Thread start synchronization event */
    HWND hNotify;           /* Window to receive WM_SERIAL_* messages */
    volatile BOOL bRunning; /* Thread running flag */
    volatile DWORD dwRxBytes; /* Total bytes received */
    volatile DWORD dwTxBytes; /* Total bytes sent */
    SERIAL_RX_CB onReceive; /* Receive data callback */
    SERIAL_SIGNAL_CB onSignal; /* Signal change callback */
} SERIAL_CTX;

/*
 * Serial_EnumPorts - Enumerate available serial ports with friendly names
 */
BOOL Serial_EnumPorts(HWND hCombo);

/*
 * Serial_Open - Open a serial port and start listener thread
 */
BOOL Serial_Open(SERIAL_CTX *ctx, const WCHAR *portName, HWND hNotify);

/*
 * Serial_Close - Close serial port and stop listener thread
 */
void Serial_Close(SERIAL_CTX *ctx);

/*
 * Serial_IsOpen - Check if port is open and running
 */
BOOL Serial_IsOpen(const SERIAL_CTX *ctx);

/*
 * Serial_GetPortName - Get port name by index
 */
BOOL Serial_GetPortName(int index, WCHAR *portName, int maxLen);

/*
 * Serial_GetRxBytes - Get total received byte count
 */
DWORD Serial_GetRxBytes(const SERIAL_CTX *ctx);

/*
 * Serial_GetTxBytes - Get total sent byte count
 */
DWORD Serial_GetTxBytes(const SERIAL_CTX *ctx);

/*
 * Serial_WriteData - Write data to serial port
 * Returns: Number of bytes written
 */
DWORD Serial_WriteData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify);

/*
 * Serial_SetReceiveCallback - Set the receive data callback
 */
void Serial_SetReceiveCallback(SERIAL_CTX *ctx, SERIAL_RX_CB cb);

/*
 * Serial_SetSignalCallback - Set the signal change callback
 *
 * Called from listener thread when EV_DSR or EV_CTS events occur.
 * Use GetCommModemStatus() flags to check signal states:
 *   MS_DSR_ON - DSR is ON (host DTR is asserted)
 *   MS_CTS_ON - CTS is ON (host RTS is asserted)
 */
void Serial_SetSignalCallback(SERIAL_CTX *ctx, SERIAL_SIGNAL_CB cb);

/*
 * Serial_SetDtr - Set or clear DTR signal
 * @state: TRUE to assert DTR, FALSE to clear
 * Returns: TRUE on success
 */
BOOL Serial_SetDtr(SERIAL_CTX *ctx, BOOL state);

/*
 * Serial_SetRts - Set or clear RTS signal
 * @state: TRUE to assert RTS, FALSE to clear
 * Returns: TRUE on success
 */
BOOL Serial_SetRts(SERIAL_CTX *ctx, BOOL state);

/*
 * Serial_SetBaudRate - Change baud rate at runtime
 * @baudRate: New baud rate (e.g. CBR_115200, CBR_921600)
 * Returns: TRUE on success
 */
BOOL Serial_SetBaudRate(SERIAL_CTX *ctx, DWORD baudRate);

/*
 * Serial_SetDataBits - Change data bits (5, 6, 7, 8)
 * Returns: TRUE on success
 */
BOOL Serial_SetDataBits(SERIAL_CTX *ctx, BYTE dataBits);

/*
 * Serial_SetParity - Change parity mode
 * @parity: NOPARITY, ODDPARITY, EVENPARITY, MARKPARITY, SPACEPARITY
 * Returns: TRUE on success
 */
BOOL Serial_SetParity(SERIAL_CTX *ctx, BYTE parity);

/*
 * Serial_SetStopBits - Change stop bits
 * @stopBits: ONESTOPBIT, ONE5STOPBITS, TWOSTOPBITS
 * Returns: TRUE on success
 */
BOOL Serial_SetStopBits(SERIAL_CTX *ctx, BYTE stopBits);

/*
 * Serial_GetConfig - Get current serial port configuration
 * @baudRate: Pointer to receive baud rate (can be NULL)
 * @dataBits: Pointer to receive data bits (can be NULL)
 * @parity: Pointer to receive parity (can be NULL)
 * @stopBits: Pointer to receive stop bits (can be NULL)
 * Returns: TRUE on success
 */
BOOL Serial_GetConfig(SERIAL_CTX *ctx, DWORD *baudRate, BYTE *dataBits, BYTE *parity, BYTE *stopBits);

/*
 * Serial_PostLog - Post a custom log message to the UI thread
 */
void Serial_PostLog(HWND hNotify, const WCHAR *tag, const WCHAR *text);

/*
 * Serial_PostLogF - Post a formatted log message to the UI thread
 *
 * Like printf, formats the message before posting.
 * Convenience wrapper around Serial_PostLog.
 *
 * @hNotify: Window handle
 * @tag: Tag text (e.g. "MODBUS", "ESP")
 * @fmt: Format string
 * @...: Format arguments
 */
void Serial_PostLogF(HWND hNotify, const WCHAR *tag, const WCHAR *fmt, ...);

#endif /* SERIAL_H */
