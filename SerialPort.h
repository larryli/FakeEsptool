/****************************************************************************\
*
*     FILE:     SerialPort.h
*
*     PURPOSE:  SerialPort IO Wrapper
*
*     COMMENTS:
*               This source is distributed in the hope that it will be useful,
*               but WITHOUT ANY WARRANTY; without even the implied warranty of
*               MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
*     Copyright 2009 David MacDermot.
*
* History:
*                Sep '09 - Created
*                Aug '10 - Fixed allocation bug in GetPortNames()
*                May '11 - Several bug fixes
*                          (labled in code as follows: //May '11 ...)
*
\****************************************************************************/
#ifndef SERIALPORT_H
#define SERIALPORT_H

/****************************************************************************/
// Public Messages

#define SPM_GETPORTNAMES WM_USER + 0x02
#define SPM_SETCONFIG WM_USER + 0x03
#define SPM_GETCONFIG WM_USER + 0x04
#define SPM_SETREADTIMEOUT WM_USER + 0x05
#define SPM_GETREADTIMEOUT WM_USER + 0x06
#define SPM_SETWRITETIMEOUT WM_USER + 0x07
#define SPM_GETWRITETIMEOUT WM_USER + 0x08
#define SPM_GETCTS WM_USER + 0x09
#define SPM_GETDSR WM_USER + 0x10
#define SPM_BYTESTOREAD WM_USER + 0x11
#define SPM_BYTESTOWRITE WM_USER + 0x12
#define SPM_WRITEBYTES WM_USER + 0x13
#define SPM_READBYTES WM_USER + 0x14
#define SPM_WRITESTRING WM_USER + 0x15
#define SPM_READSTRING WM_USER + 0x16
#define SPM_CLOSE WM_USER + 0x17
#define SPM_OPEN WM_USER + 0x18
#define SPM_FLUSH WM_USER + 0x19
#define SPM_SETRXTHRESHOLD WM_USER + 0x1A
#define SPM_GETRXTHRESHOLD WM_USER + 0x1B
#define SPM_GETPORTSEX WM_USER + 0x1C

/****************************************************************************/
// Public Enums

typedef enum tagFlowControl {
    NoFlowControl,
    CtsRtsFlowControl,
    CtsDtrFlowControl,
    DsrRtsFlowControl,
    DsrDtrFlowControl,
    XonXoffFlowControl
} SPFLOWCONTROL;

/****************************************************************************/
// Public Structs

typedef struct tagConfig {
    UINT mask;
    LPTSTR pszPortName;
    INT cchTextMax;
    DWORD dwBaudRate;
    BYTE bParity;
    BYTE bDataBits;
    BYTE bStopBits;
    BOOL fDiscardNull;
    SPFLOWCONTROL flowControl;
} SPCONFIG, *LPSPCONFIG;

typedef struct tagPortEx {
    LPTSTR pszPortName;
    BOOL bPresent;
    DWORD dwVendorId;
    DWORD dwProductId;
    LPTSTR pszFriendlyName;
} SPPORTEX, *LPSPPORTEX;

typedef struct tagPortExParams {
    UINT mask;
    BOOL bPresent;
    DWORD dwVendorId;
    DWORD dwProductId;
} SPPORTEXPARAMS, *LPSPPORTEXPARAMS;

// Mask values
#define SPCF_PORTNAME 0x0001
#define SPCF_BAUDRATE 0x0002
#define SPCF_PARITY 0x0004
#define SPCF_DATABITS 0x0008
#define SPCF_STOPBITS 0x0010
#define SPCF_NULLDISCARD 0x0020
#define SPCF_FLOWCONT 0x0040

#define SPCF_ALLSETTINGS                                                       \
    SPCF_PORTNAME | SPCF_BAUDRATE | SPCF_PARITY | SPCF_DATABITS |              \
        SPCF_STOPBITS | SPCF_NULLDISCARD | SPCF_FLOWCONT

#define SPPF_PRESENT 0x0001
#define SPPF_VENDORID 0x0002
#define SPPF_PRODUCTID 0x0004

/****************************************************************************/
// Notifications

typedef struct tagNMSERIAL {
    NMHDR hdr;
    DWORD dwCode;
} SPNMSERIAL, *LPSPNMSERIAL;

#define SPN_DATARECEIVED WM_USER + 0x2A
#define SPN_PINCHANGED WM_USER + 0x2B
#define SPN_ERRORRECEIVED WM_USER + 0x2C

#ifndef HANDLE_WM_DEVICECHANGE
#define HANDLE_WM_DEVICECHANGE(hwnd, wParam, lParam, fn)                       \
    (LRESULT)(DWORD)(BOOL)(fn)((hwnd), (UINT)(wParam), (DWORD)(lParam))
#endif

/****************************************************************************/
// Macroes

#define SerialPort_GetPortNames(hwnd, lpCount)                                 \
    (LPTSTR *)SNDMSG((hwnd), SPM_GETPORTNAMES, (WPARAM)(lpCount), (LPARAM)0L)
#define SerialPort_SetConfigurations(hwnd, lpConfig)                           \
    (BOOL) SNDMSG((hwnd), SPM_SETCONFIG, (WPARAM)0,                            \
                  (LPARAM)(const LPCONFIG)(lpConfig))
#define SerialPort_GetConfigurations(hwnd, lpConfig)                           \
    (BOOL)                                                                     \
        SNDMSG((hwnd), SPM_GETCONFIG, (WPARAM)0, (LPARAM)(LPCONFIG)(lpConfig))
#define SerialPort_SetReadTimeout(hwnd, dwTimeout)                             \
    (BOOL) SNDMSG((hwnd), SPM_SETREADTIMEOUT, (WPARAM)(dwTimeout), (LPARAM)0L)
#define SerialPort_GetReadTimeout(hwnd)                                        \
    (DWORD) SNDMSG((hwnd), SPM_GETREADTIMEOUT, (WPARAM)0, (LPARAM)0L)
#define SerialPort_SetWriteTimeout(hwnd, dwTimeout)                            \
    (BOOL) SNDMSG((hwnd), SPM_SETWRITETIMEOUT, (WPARAM)(dwTimeout), (LPARAM)0L)
#define SerialPort_GetWriteTimeout(hwnd)                                       \
    (DWORD) SNDMSG((hwnd), SPM_GETWRITETIMEOUT, (WPARAM)0, (LPARAM)0L)
#define SerialPort_GetCTS(hwnd)                                                \
    (BOOL) SNDMSG((hwnd), SPM_GETCTS, (WPARAM)0, (LPARAM)0L)
#define SerialPort_GetDSR(hwnd)                                                \
    (BOOL) SNDMSG((hwnd), SPM_GETDSR, (WPARAM)0, (LPARAM)0L)
#define SerialPort_BytesToRead(hwnd)                                           \
    (DWORD) SNDMSG((hwnd), SPM_BYTESTOREAD, (WPARAM)0, (LPARAM)0L)
#define SerialPort_BytesToWrite(hwnd)                                          \
    (DWORD) SNDMSG((hwnd), SPM_BYTESTOWRITE, (WPARAM)0, (LPARAM)0L)
#define SerialPort_WriteBytes(hwnd, lpData, dwSize)                            \
    (BOOL) SNDMSG((hwnd), SPM_WRITEBYTES, (WPARAM)(dwSize), (LPARAM)(lpData))
#define SerialPort_ReadBytes(hwnd, lpBuf, dwSize)                              \
    (DWORD) SNDMSG((hwnd), SPM_READBYTES, (WPARAM)(dwSize), (LPARAM)(lpBuf))
#define SerialPort_WriteString(hwnd, lpsztext)                                 \
    (BOOL) SNDMSG((hwnd), SPM_WRITESTRING, (WPARAM)0, (LPARAM)(lpsztext))
#define SerialPort_ReadString(hwnd, lpszBuf, dwSize)                           \
    (DWORD) SNDMSG((hwnd), SPM_READSTRING, (WPARAM)(dwSize), (LPARAM)(lpszBuf))
#define SerialPort_Close(hwnd)                                                 \
    (BOOL) SNDMSG((hwnd), SPM_CLOSE, (WPARAM)0, (LPARAM)0L)
#define SerialPort_Open(hwnd, lpConfig)                                        \
    (BOOL) SNDMSG((hwnd), SPM_OPEN, (WPARAM)0, (LPARAM)(lpConfig))
#define SerialPort_FlushReadBuf(hwnd)                                          \
    (BOOL) SNDMSG((hwnd), SPM_FLUSH, (WPARAM)PURGE_RXCLEAR, (LPARAM)0L)
#define SerialPort_FlushWriteBuf(hwnd)                                         \
    (BOOL) SNDMSG((hwnd), SPM_FLUSH, (WPARAM)PURGE_TXCLEAR, (LPARAM)0L)
#define SerialPort_SetReceiveByteThreshold(hwnd, dwNumBytes)                   \
    (BOOL) SNDMSG((hwnd), SPM_SETRXTHRESHOLD, (WPARAM)(dwNumBytes), (LPARAM)0L)
#define SerialPort_GetReceiveByteThreshold(hwnd)                               \
    (DWORD) SNDMSG((hwnd), SPM_GETRXTHRESHOLD, (WPARAM)0, (LPARAM)0L)
#define SerialPort_GetPortsEx(hwnd, lpExParams, lpCount)                       \
    (LPSPPORTEX *)SNDMSG((hwnd), SPM_GETPORTSEX, (WPARAM)(lpCount),            \
                         (LPARAM)(lpExParams))

/****************************************************************************/
// Prototypes

ATOM InitSerialPortControl(HINSTANCE);
HWND New_SerialPortControl(HWND, DWORD);

#endif // SERIALPORT_H
