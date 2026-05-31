/*
 * serial.c - Serial port communication module
 *
 * Implements serial port enumeration, open/close, and data reception
 * using WaitCommEvent-based event-driven I/O.
 *
 * Received data is passed to protocol handler for processing.
 */

#include "serial.h"
#include "main.h"
#include "resource.h"
#include "utils/trace.h"
#include <setupapi.h>
#include <devguid.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")

static const char *TAG = "SER";

#define READ_BUFFER_SIZE 4096
#define MAX_PORTS 64

/* Port info structure for friendly name display */
typedef struct {
    WCHAR portName[32];
    WCHAR friendlyName[128];
} PORT_INFO;

static PORT_INFO g_portInfo[MAX_PORTS];
static int g_portCount = 0;

/*
 * Serial_EnumPorts - Enumerate available serial ports with friendly names
 *
 * Uses SetupDi API to get device friendly names from registry.
 */
BOOL Serial_EnumPorts(HWND hCombo)
{
    g_portCount = 0;
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

    HDEVINFO devInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE)
        return FALSE;

    SP_DEVINFO_DATA devInfoData = { .cbSize = sizeof(SP_DEVINFO_DATA) };
    BOOL found = FALSE;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData) && g_portCount < MAX_PORTS; i++) {
        /* Get port name from registry */
        HKEY hKey = SetupDiOpenDevRegKey(devInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey == INVALID_HANDLE_VALUE)
            continue;

        WCHAR portName[32] = {0};
        DWORD size = sizeof(portName);
        DWORD type = 0;
        LONG ret = RegQueryValueExW(hKey, L"PortName", NULL, &type, (LPBYTE)portName, &size);
        RegCloseKey(hKey);

        if (ret != ERROR_SUCCESS || type != REG_SZ)
            continue;

        /* Get friendly name */
        WCHAR friendlyName[128] = {0};
        DWORD friendlySize = sizeof(friendlyName);
        if (!SetupDiGetDeviceRegistryPropertyW(devInfo, &devInfoData, SPDRP_FRIENDLYNAME,
                                               NULL, (PBYTE)friendlyName, friendlySize, NULL)) {
            /* Fallback to port name if no friendly name */
            lstrcpyW(friendlyName, portName);
        }

        /* Store port info */
        lstrcpyW(g_portInfo[g_portCount].portName, portName);
        lstrcpyW(g_portInfo[g_portCount].friendlyName, friendlyName);

        /* Add to combo box: friendly name only */
        int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)friendlyName);
        if (idx >= 0) {
            SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)g_portCount);
            found = TRUE;
        }
        g_portCount++;
    }

    SetupDiDestroyDeviceInfoList(devInfo);

    if (found)
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

    return found;
}

/*
 * Listener_Proc - Monitor serial port using WaitCommEvent
 *
 * This thread waits for comm events (EV_RXCHAR, etc.) and posts
 * notifications to the UI thread.
 */
static DWORD WINAPI Listener_Proc(LPVOID param)
{
    SERIAL_CTX *ctx = (SERIAL_CTX *)param;
    BYTE buffer[READ_BUFFER_SIZE];
    DWORD dwEvtMask = 0;
    OVERLAPPED ov = {0};
    HANDLE hReadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    BOOL errorExit = FALSE;

    TRACE_FW(TAG, "Listener started");

    if (!hReadEvent) {
        TRACE_FW(TAG, "ERROR: Failed to create event");
        return 1;
    }

    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        TRACE_FW(TAG, "ERROR: Failed to create ov.hEvent");
        CloseHandle(hReadEvent);
        return 1;
    }

    /* Signal that thread has started */
    SetEvent(ctx->hStartEvent);

    while (ctx->bRunning) {
        /* Set comm mask to listen for receive and signal events */
        if (!SetCommMask(ctx->hPort, EV_RXCHAR | EV_ERR | EV_DSR | EV_CTS | EV_RING | EV_RLSD)) {
            errorExit = TRUE;
            break;
        }

        /* Wait for comm event */
        ResetEvent(ov.hEvent);

        if (!WaitCommEvent(ctx->hPort, &dwEvtMask, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                DWORD waitResult = WaitForSingleObject(ov.hEvent, 100);
                if (waitResult == WAIT_TIMEOUT)
                    continue;
                if (waitResult != WAIT_OBJECT_0) {
                    errorExit = (waitResult == WAIT_FAILED);
                    break;
                }
                if (!GetOverlappedResult(ctx->hPort, &ov, &(DWORD){0}, FALSE)) {
                    errorExit = TRUE;
                    break;
                }
            } else if (GetLastError() == ERROR_OPERATION_ABORTED) {
                /* Normal shutdown via CancelIo */
                break;
            } else {
                TRACE_FW(TAG, "ERROR: WaitCommEvent failed: %lu", GetLastError());
                errorExit = TRUE;
                break;
            }
        }

        if (!ctx->bRunning)
            break;

        /* Handle received data */
        if (dwEvtMask & EV_RXCHAR) {
            DWORD bytesRead = 0;
            COMSTAT comStat;
            DWORD dwErrors;

            if (!ClearCommError(ctx->hPort, &dwErrors, &comStat)) {
                errorExit = TRUE;
                break;
            }

            if (comStat.cbInQue > 0 && comStat.cbInQue < READ_BUFFER_SIZE) {
                DWORD toRead = comStat.cbInQue;
                OVERLAPPED ovRead = {0};
                ovRead.hEvent = hReadEvent;
                ResetEvent(hReadEvent);

                if (ReadFile(ctx->hPort, buffer, toRead, &bytesRead, &ovRead)) {
                    /* Read completed synchronously */
                } else if (GetLastError() == ERROR_IO_PENDING) {
                    if (WaitForSingleObject(hReadEvent, 1000) != WAIT_OBJECT_0)
                        continue;
                    if (!GetOverlappedResult(ctx->hPort, &ovRead, &bytesRead, FALSE)) {
                        errorExit = TRUE;
                        continue;
                    }
                } else {
                    continue;
                }

                if (bytesRead > 0 && ctx->bRunning) {
                    /* Post RX data to UI thread for logging */
                    if (ctx->hNotify && IsWindow(ctx->hNotify)) {
                        size_t allocSize = (size_t)bytesRead + 1;
                        void *copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, allocSize);
                        if (copy) {
                            CopyMemory(copy, buffer, bytesRead);
                            if (!PostMessage(ctx->hNotify, WM_SERIAL_RX, (WPARAM)bytesRead, (LPARAM)copy)) {
                                TRACE_FW(TAG, "ERROR: PostMessage RX failed: %lu", GetLastError());
                                HeapFree(GetProcessHeap(), 0, copy);
                            }
                        }
                    }

                    /*
                     * ============================================================
                     * RECEIVE CALLBACK - Extension Point
                     * ============================================================
                     *
                     * Call the registered callback to process received data.
                     * Set callback via Serial_SetReceiveCallback().
                     *
                     * Current: esptool protocol (esptool_proto.c)
                     *
                     * To customize: implement your own callback and register it.
                     *
                     * ============================================================
                     */
                    if (ctx->onReceive)
                        ctx->onReceive(ctx, buffer, bytesRead, ctx->hNotify);

                    ctx->dwRxBytes += bytesRead;
                }
            }
        }

        /* Handle errors */
        if (dwEvtMask & EV_ERR) {
            DWORD dwErrors;
            ClearCommError(ctx->hPort, &dwErrors, NULL);
            TRACE_FW(TAG, "Comm error: 0x%08lX", dwErrors);
        }

        /* Handle signal changes (DSR/CTS/RI/DCD from host) */
        if (dwEvtMask & (EV_DSR | EV_CTS | EV_RING | EV_RLSD)) {
            DWORD modemStatus = 0;
            if (GetCommModemStatus(ctx->hPort, &modemStatus)) {
                /* Notify UI to display signal change: wParam=0 for host signals */
                if (ctx->hNotify && IsWindow(ctx->hNotify)) {
                    PostMessage(ctx->hNotify, WM_SERIAL_SIGNAL, 0, (LPARAM)modemStatus);
                }

                /* Call signal callback */
                if (ctx->onSignal)
                    ctx->onSignal(ctx, modemStatus, ctx->hNotify);
            }
        }
    }

    TRACE_FW(TAG, "Listener exiting (error=%d)", errorExit);
    CloseHandle(ov.hEvent);
    CloseHandle(hReadEvent);

    /* Notify UI if this was an unexpected exit */
    if (errorExit && ctx->hNotify && IsWindow(ctx->hNotify)) {
        DWORD lastErr = GetLastError();
        PostMessage(ctx->hNotify, WM_SERIAL_ERROR, (WPARAM)lastErr, 0);
    }

    return 0;
}

/*
 * Serial_Open - Open a serial port with 115200,8N1 configuration
 */
BOOL Serial_Open(SERIAL_CTX *ctx, const WCHAR *portName, HWND hNotify)
{
    TRACE_FW(TAG, "Opening port: %s", portName);

    if (ctx->hPort != INVALID_HANDLE_VALUE && ctx->hPort != NULL)
        return FALSE;

    /* Create events */
    ctx->hStartEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ctx->hStartEvent) {
        return FALSE;
    }

    /* Open port with \\.\ prefix */
    WCHAR fullPort[32];
    wsprintfW(fullPort, L"\\\\.\\%s", portName);

    ctx->hPort = CreateFileW(fullPort, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (ctx->hPort == INVALID_HANDLE_VALUE) {
        TRACE_FW(TAG, "ERROR: CreateFileW failed: %lu", GetLastError());
        CloseHandle(ctx->hStartEvent);
                return FALSE;
    }

    /* Configure serial port: 115200 baud, 8 data bits, no parity, 1 stop bit */
    DCB dcb = { .DCBlength = sizeof(DCB) };
    if (!GetCommState(ctx->hPort, &dcb)) {
        CloseHandle(ctx->hPort);
        ctx->hPort = INVALID_HANDLE_VALUE;
        CloseHandle(ctx->hStartEvent);
                return FALSE;
    }

    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    if (!SetCommState(ctx->hPort, &dcb)) {
        CloseHandle(ctx->hPort);
        ctx->hPort = INVALID_HANDLE_VALUE;
        CloseHandle(ctx->hStartEvent);
                return FALSE;
    }

    /* Set timeouts */
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 100;
    SetCommTimeouts(ctx->hPort, &timeouts);

    /* Setup comm buffers */
    SetupComm(ctx->hPort, READ_BUFFER_SIZE, READ_BUFFER_SIZE);

    /* Purge any existing data */
    PurgeComm(ctx->hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

    /* Set DTR and RTS */
    EscapeCommFunction(ctx->hPort, SETDTR);
    EscapeCommFunction(ctx->hPort, SETRTS);

    /* Start listener thread */
    ctx->hNotify = hNotify;
    ctx->bRunning = TRUE;
    ctx->dwRxBytes = 0;
    ctx->dwTxBytes = 0;
    ctx->onReceive = NULL;
    ctx->onSignal = NULL;

    ctx->hThread = CreateThread(NULL, 0, Listener_Proc, ctx, 0, NULL);
    if (!ctx->hThread) {
        TRACE_FW(TAG, "ERROR: CreateThread failed: %lu", GetLastError());
        ctx->bRunning = FALSE;
        CloseHandle(ctx->hPort);
        ctx->hPort = INVALID_HANDLE_VALUE;
        CloseHandle(ctx->hStartEvent);
                return FALSE;
    }

    /* Wait for thread to start */
    WaitForSingleObject(ctx->hStartEvent, 1000);
    TRACE_FW(TAG, "Port opened successfully");

    return TRUE;
}

/*
 * Serial_Close - Close serial port and stop listener thread
 */
void Serial_Close(SERIAL_CTX *ctx)
{
    TRACE_FW(TAG, "Closing port");

    if (ctx->bRunning) {
        ctx->bRunning = FALSE;

        /* Unblock WaitCommEvent by clearing comm mask */
        if (ctx->hPort != INVALID_HANDLE_VALUE && ctx->hPort != NULL) {
            SetCommMask(ctx->hPort, 0);
            EscapeCommFunction(ctx->hPort, CLRDTR);
            PurgeComm(ctx->hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);
            CancelIo(ctx->hPort);
        }

        /* Wait for thread to exit */
        if (ctx->hThread) {
            WaitForSingleObject(ctx->hThread, 5000);
            CloseHandle(ctx->hThread);
            ctx->hThread = NULL;
        }

        /* Thread has exited, now safe to clear notify handle */
        ctx->hNotify = NULL;
    }

    /* Close port handle */
    if (ctx->hPort != INVALID_HANDLE_VALUE && ctx->hPort != NULL) {
        CloseHandle(ctx->hPort);
        ctx->hPort = INVALID_HANDLE_VALUE;
    }

    /* Close events */
    if (ctx->hStartEvent) {
        CloseHandle(ctx->hStartEvent);
        ctx->hStartEvent = NULL;
    }
}

/* Check if serial port is open */
BOOL Serial_IsOpen(const SERIAL_CTX *ctx)
{
    return (ctx->hPort != INVALID_HANDLE_VALUE && ctx->hPort != NULL && ctx->bRunning);
}

/* Get total received bytes */
DWORD Serial_GetRxBytes(const SERIAL_CTX *ctx)
{
    return ctx->dwRxBytes;
}

/* Get port name by index */
BOOL Serial_GetPortName(int index, WCHAR *portName, int maxLen)
{
    if (index < 0 || index >= g_portCount)
        return FALSE;
    lstrcpynW(portName, g_portInfo[index].portName, maxLen);
    return TRUE;
}

/* Get total sent bytes */
DWORD Serial_GetTxBytes(const SERIAL_CTX *ctx)
{
    return ctx->dwTxBytes;
}

/* Write data to serial port */
DWORD Serial_WriteData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return 0;
    if (!data || len == 0)
        return 0;

    OVERLAPPED ov = {0};
    HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!hEvent)
        return 0;
    ov.hEvent = hEvent;

    DWORD bytesWritten = 0;
    BOOL result = WriteFile(ctx->hPort, data, len, &bytesWritten, &ov);
    if (!result && GetLastError() == ERROR_IO_PENDING) {
        WaitForSingleObject(hEvent, 1000);
        result = GetOverlappedResult(ctx->hPort, &ov, &bytesWritten, FALSE);
    }
    CloseHandle(hEvent);

    if (result && bytesWritten > 0) {
        ctx->dwTxBytes += bytesWritten;

        /* Post TX data to UI thread for logging */
        if (hNotify && IsWindow(hNotify)) {
            void *copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytesWritten + 1);
            if (copy) {
                CopyMemory(copy, data, bytesWritten);
                if (!PostMessage(hNotify, WM_SERIAL_TX, (WPARAM)bytesWritten, (LPARAM)copy)) {
                    HeapFree(GetProcessHeap(), 0, copy);
                }
            }
        }
    }

    return bytesWritten;
}

/* Set the receive data callback */
void Serial_SetReceiveCallback(SERIAL_CTX *ctx, SERIAL_RX_CB cb)
{
    if (ctx)
        ctx->onReceive = cb;
}

/* Post a custom log message to the UI thread */
void Serial_PostLog(HWND hNotify, const WCHAR *tag, const WCHAR *text)
{
    if (!hNotify || !IsWindow(hNotify) || !tag || !text)
        return;

    /* Allocate copies for the message */
    size_t tagLen = (lstrlenW(tag) + 1) * sizeof(WCHAR);
    size_t textLen = (lstrlenW(text) + 1) * sizeof(WCHAR);
    WCHAR *tagCopy = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, tagLen);
    WCHAR *textCopy = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, textLen);

    if (tagCopy && textCopy) {
        CopyMemory(tagCopy, tag, tagLen);
        CopyMemory(textCopy, text, textLen);
        if (!PostMessage(hNotify, WM_SERIAL_LOG, (WPARAM)tagCopy, (LPARAM)textCopy)) {
            HeapFree(GetProcessHeap(), 0, tagCopy);
            HeapFree(GetProcessHeap(), 0, textCopy);
        }
    } else {
        if (tagCopy) HeapFree(GetProcessHeap(), 0, tagCopy);
        if (textCopy) HeapFree(GetProcessHeap(), 0, textCopy);
    }
}

/* Post a formatted log message to the UI thread */
#define LOGF_BUF_SIZE 4096

void Serial_PostLogF(HWND hNotify, const WCHAR *tag, const WCHAR *fmt, ...)
{
    if (!hNotify || !tag || !fmt)
        return;

    WCHAR buf[LOGF_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    wvsprintfW(buf, fmt, args);
    va_end(args);

    Serial_PostLog(hNotify, tag, buf);
}

/* Set the signal change callback */
void Serial_SetSignalCallback(SERIAL_CTX *ctx, SERIAL_SIGNAL_CB cb)
{
    if (ctx)
        ctx->onSignal = cb;
}

/* Set or clear DTR signal */
BOOL Serial_SetDtr(SERIAL_CTX *ctx, BOOL state)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return FALSE;

    BOOL result = EscapeCommFunction(ctx->hPort, state ? SETDTR : CLRDTR);
    if (result && ctx->hNotify && IsWindow(ctx->hNotify)) {
        /* Post signal change: wParam = 1 for DTR, lParam = state */
        PostMessage(ctx->hNotify, WM_SERIAL_SIGNAL, 1, (LPARAM)state);
    }
    return result;
}

/* Set or clear RTS signal */
BOOL Serial_SetRts(SERIAL_CTX *ctx, BOOL state)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return FALSE;

    BOOL result = EscapeCommFunction(ctx->hPort, state ? SETRTS : CLRRTS);
    if (result && ctx->hNotify && IsWindow(ctx->hNotify)) {
        /* Post signal change: wParam = 2 for RTS, lParam = state */
        PostMessage(ctx->hNotify, WM_SERIAL_SIGNAL, 2, (LPARAM)state);
    }
    return result;
}

/* Change baud rate at runtime */
BOOL Serial_SetBaudRate(SERIAL_CTX *ctx, DWORD baudRate)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return FALSE;

    DCB dcb = { .DCBlength = sizeof(DCB) };
    if (!GetCommState(ctx->hPort, &dcb))
        return FALSE;

    dcb.BaudRate = baudRate;
    if (!SetCommState(ctx->hPort, &dcb))
        return FALSE;

    /* Notify UI to update config display */
    if (ctx->hNotify && IsWindow(ctx->hNotify)) {
        PostMessage(ctx->hNotify, WM_SERIAL_CONFIG, (WPARAM)baudRate, 0);
    }

    return TRUE;
}

/* Change data bits at runtime */
BOOL Serial_SetDataBits(SERIAL_CTX *ctx, BYTE dataBits)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return FALSE;
    if (dataBits < 5 || dataBits > 8)
        return FALSE;

    DCB dcb = { .DCBlength = sizeof(DCB) };
    if (!GetCommState(ctx->hPort, &dcb))
        return FALSE;

    dcb.ByteSize = dataBits;
    if (!SetCommState(ctx->hPort, &dcb))
        return FALSE;

    /* Notify UI to update config display */
    if (ctx->hNotify && IsWindow(ctx->hNotify)) {
        PostMessage(ctx->hNotify, WM_SERIAL_CONFIG, 0, 0);
    }

    return TRUE;
}

/* Change parity mode */
BOOL Serial_SetParity(SERIAL_CTX *ctx, BYTE parity)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return FALSE;

    DCB dcb = { .DCBlength = sizeof(DCB) };
    if (!GetCommState(ctx->hPort, &dcb))
        return FALSE;

    dcb.Parity = parity;
    dcb.fParity = (parity != NOPARITY);
    if (!SetCommState(ctx->hPort, &dcb))
        return FALSE;

    /* Notify UI to update config display */
    if (ctx->hNotify && IsWindow(ctx->hNotify)) {
        PostMessage(ctx->hNotify, WM_SERIAL_CONFIG, 0, 0);
    }

    return TRUE;
}

/* Change stop bits */
BOOL Serial_SetStopBits(SERIAL_CTX *ctx, BYTE stopBits)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return FALSE;

    DCB dcb = { .DCBlength = sizeof(DCB) };
    if (!GetCommState(ctx->hPort, &dcb))
        return FALSE;

    dcb.StopBits = stopBits;
    if (!SetCommState(ctx->hPort, &dcb))
        return FALSE;

    /* Notify UI to update config display */
    if (ctx->hNotify && IsWindow(ctx->hNotify)) {
        PostMessage(ctx->hNotify, WM_SERIAL_CONFIG, 0, 0);
    }

    return TRUE;
}

/* Get current serial port configuration */
BOOL Serial_GetConfig(SERIAL_CTX *ctx, DWORD *baudRate, BYTE *dataBits, BYTE *parity, BYTE *stopBits)
{
    if (!ctx || ctx->hPort == INVALID_HANDLE_VALUE || ctx->hPort == NULL)
        return FALSE;

    DCB dcb = { .DCBlength = sizeof(DCB) };
    if (!GetCommState(ctx->hPort, &dcb))
        return FALSE;

    if (baudRate) *baudRate = dcb.BaudRate;
    if (dataBits) *dataBits = dcb.ByteSize;
    if (parity) *parity = dcb.Parity;
    if (stopBits) *stopBits = dcb.StopBits;

    return TRUE;
}
