/****************************************************************************\
*
*     FILE:     SerialPort.c
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
// #define UNICODE
// #define _UNICODE

#define WIN32_LEAN_AND_MEAN
#define NOCRYPT
#define NOSERVICE
#define NOMCX
#define NOIME

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>

#include <commctrl.h>
#include <setupapi.h>

#include "SerialPort.h"

#pragma comment(lib, "setupapi.lib")

#ifndef SPDRP_INSTALL_STATE
#define SPDRP_INSTALL_STATE (0x00000022)
#endif

#define NELEMS(a) (sizeof(a) / sizeof((a)[0]))

/****************************************************************************/
// Private Messages

#define SPM_DISPATCHNOTIFICATIONS WM_USER + 0x01

/****************************************************************************/
// Globals

LPCTSTR g_szClassName = _T("SerialPortCtl");

#pragma region Instance Data

/****************************************************************************/
// SerialCommPort.c private Structure

typedef struct _tagINSTANCEDATA {
    HWND hwndParent;
    LPTSTR lpComm;
    HANDLE hComm;
    BOOL fEndListner;     // May '11 Added
    HANDLE hListnerEvent; // May '11 Added
    HANDLE hEventThread;
    HANDLE hStartEvent;
    HANDLE hIOEvent;
    DWORD dwReceiveByteThreshold;
    DWORD dwEventThread;
    LPTSTR *lpPortList;
    DWORD dwPortCount;
    LPSPPORTEX *lpPortExList;
    DWORD dwPortExCount;
    SPFLOWCONTROL flowControl;
} INSTANCEDATA, *LPINSTANCEDATA;

LPINSTANCEDATA g_lpInst;

/****************************************************************************
*
*     FUNCTION: Control_GetInstanceData
*
*     PURPOSE:  Get the Instance data associated with this instance.
*
*     PARAMS:   HWND hControl - Handle to Current instance
*               LPINSTANCEDATA *ppInstanceData - pointer to the address of an
INSTANCEDATA struct
*
*     RETURNS:  BOOL - TRUE if successful
*
* History:
*                Jan '08 - Created
*
\****************************************************************************/

static BOOL Control_GetInstanceData(HWND hControl,
                                    LPINSTANCEDATA *ppInstanceData)
{
    *ppInstanceData =
        (LPINSTANCEDATA)GetProp(hControl, (LPCTSTR) _T("lpInsData"));
    if (NULL != *ppInstanceData)
        return TRUE;
    return FALSE;
}

/****************************************************************************
*
*     FUNCTION: Control_CreateInstanceData
*
*     PURPOSE:  Allocate the Instance data associated with this instance.
*
*     PARAMS:   HWND hControl - Handle to Current instance
*               LPINSTANCEDATA pInstanceData - pointer to an INSTANCEDATA struct
*
*     RETURNS:  BOOL - TRUE if successful
*
* History:
*                Jan '08 - Created
*
\****************************************************************************/

static BOOL Control_CreateInstanceData(HWND hControl,
                                       LPINSTANCEDATA pInstanceData)
{
    LPINSTANCEDATA pInst = (LPINSTANCEDATA)malloc(sizeof(INSTANCEDATA));

    if (NULL == pInst) {
        return FALSE;
    }

    memmove(pInst, pInstanceData, sizeof(INSTANCEDATA));

    return SetProp(hControl, (LPCTSTR) _T("lpInsData"), pInst);
}

/****************************************************************************
*
*     FUNCTION: Control_FreeInstanceData
*
*     PURPOSE:  Free the instance data allocation of an instance of the Grid
Control.
*
*     PARAMS:   HWND hControl - Handle to Current instance
*
*     RETURNS:  BOOL - TRUE if successful
*
* History:
*                Jan '08 - Created
*
\****************************************************************************/

static BOOL Control_FreeInstanceData(HWND hControl)
{
    LPINSTANCEDATA pInst;
    if (Control_GetInstanceData(hControl, &pInst)) {
        free((LPINSTANCEDATA)pInst);
        RemoveProp(hControl, (LPCTSTR) _T("lpInsData"));
        return TRUE;
    }
    return FALSE;
}

/****************************************************************************
*
*     FUNCTION: SetStringBuffer
*
*     PURPOSE:  Allocate and store a buffer of text
*
*     PARAMS:   LPTSTR lpBuf - Pointer to buffer
*               LPTSTR value - A string to store
*
*     RETURNS:  VOID
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

static VOID SetStringBuffer(LPTSTR *lppBuf, LPTSTR value)
{
    INT len = _tcslen(value) + 1;
    INT size = len * sizeof(TCHAR);
    *lppBuf = (LPTSTR)realloc(*lppBuf, size);
    _tcsncpy(*lppBuf, value, len);
}

#pragma endregion Instance Data

#pragma region Port Names

/****************************************************************************
*
*     FUNCTION: GetPortNames
*
*     PURPOSE:  Get a list of comm ports available on host workstation.
*
*     PARAMS:   LPTSTR **lpPortList - a referance pointer to receive the list of
port names
*               LPDWORD lpCount - a referance DWORD pointer to receive the list
size
*
*     RETURNS:  LONG - ERROR_SUCCESS if successful. If the function fails,
*                      the return value is a nonzero error code defined in
WINERROR.H.
*
* History:
*                Jan '08 - Created
*
\****************************************************************************/

LONG GetPortNames(LPTSTR **lpPortList, LPDWORD lpCount)
{
    HKEY hKey;
    LONG res;
    DWORD myType;
    TCHAR regValue[MAX_PATH];
    TCHAR portName[MAX_PATH];

    // open the regkey where the serial port data is stored
    res =
        RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"),
                     0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS) {
        return res;
    }

    __try {
        // get the number of ports
        res = RegQueryInfoKey(hKey, NULL, NULL, NULL, NULL, NULL, NULL, lpCount,
                              NULL, NULL, NULL, NULL);
        if (res != ERROR_SUCCESS) {
            __leave;
        }

        // create array based on number of ports
        *lpPortList = (LPTSTR *)malloc((*lpCount + 1) * sizeof(PTCHAR));
        (*lpPortList)[*lpCount] = NULL; // May '11 So FreePortNameList() will
                                        // stop at the end of the list

        if (NULL == *lpPortList) {
            return 0;
        }

        myType = REG_SZ;

        for (int port = 0, lenValue, lenName; port < (int)*lpCount; port++) {
            lenValue = NELEMS(regValue);
            lenName = NELEMS(portName);

            // cycle through reg values to get port names
            res = RegEnumValue(hKey, port, regValue, (LPDWORD)&lenValue, NULL,
                               &myType, (LPBYTE)portName, (LPDWORD)&lenName);
            if (res != ERROR_SUCCESS) {
                __leave;
            }

            // allocate and populate an array index
            (*lpPortList)[port] =
                (LPTSTR)calloc((size_t)lenName, sizeof(TCHAR));

            if (NULL == (*lpPortList)[port]) {
                continue;
            }

            memmove((*lpPortList)[port], portName, lenName);
        }
    } __finally {
        RegCloseKey(hKey);
    }
    return res;
}

/****************************************************************************
*
*     FUNCTION: FreePortNameList
*
*     PURPOSE:  Free the memory allocated for the list of available comm ports.
*
*     PARAMS:   LPTSTR *portList - the allocated list of port names
*
*     RETURNS:  VOID
*
* History:
*                Jan '08 - Created
*
\****************************************************************************/

VOID FreePortNameList(LPTSTR *portList)
{
    int Count = 0;
    while (portList[Count]) {
        free(portList[Count++]);
    }
    free(portList);
}

VOID FreePortExList(LPSPPORTEX *portExList)
{
    int Count = 0;
    while (portExList[Count]) {
        if (portExList[Count]->pszPortName) {
            free(portExList[Count]->pszPortName);
        }
        if (portExList[Count]->pszFriendlyName) {
            free(portExList[Count]->pszFriendlyName);
        }
        free(portExList[Count++]);
    }
    free(portExList);
}

static LONG GetRegValueStr(HKEY hKey, PTSTR pName, PTSTR *pValue)
{
    DWORD cbValue = 0;
    LONG ret = RegQueryValueEx(hKey, pName, NULL, NULL, NULL, &cbValue);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    *pValue = (PTSTR)malloc(cbValue);
    if (*pValue == NULL) {
        return GetLastError();
    }
    ret = RegQueryValueEx(hKey, pName, NULL, NULL, (PBYTE)*pValue, &cbValue);
    if (ret != ERROR_SUCCESS) {
        free(*pValue);
        *pValue = NULL;
    }
    return ret;
}

static BOOL GetDeviceRegPropVar(HDEVINFO hDevInfo,
                                PSP_DEVINFO_DATA pDevInfoData, DWORD dwProp,
                                DWORD *pValue)
{
    return SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, dwProp,
                                            NULL, (PBYTE)pValue, sizeof(DWORD),
                                            NULL);
}

static BOOL GetDeviceRegPropStr(HDEVINFO hDevInfo,
                                PSP_DEVINFO_DATA pDevInfoData, DWORD dwProp,
                                PTSTR *pValue)
{
    DWORD cbValue = 0;
    SetupDiGetDeviceRegistryProperty(hDevInfo, pDevInfoData, dwProp, NULL, NULL,
                                     0, &cbValue);
    if (cbValue == 0) {
        return FALSE;
    }
    *pValue = (PTSTR)malloc(cbValue);
    if (*pValue == NULL) {
        return FALSE;
    }
    BOOL ret = SetupDiGetDeviceRegistryProperty(
        hDevInfo, pDevInfoData, dwProp, NULL, (PBYTE)*pValue, cbValue, NULL);
    if (ret == FALSE) {
        free(*pValue);
        *pValue = NULL;
    }
    return ret;
}

#define CAPS 10

BOOL GetPortsEx(LPSPPORTEX **lpPortExList, LPSPPORTEXPARAMS lpPortExParams,
                LPDWORD lpExCount)
{
    DWORD n = 0, caps = CAPS;
    LPSPPORTEX *lpList = (LPSPPORTEX *)malloc(caps * sizeof(LPSPPORTEX));
    if (lpList == NULL) {
        return FALSE;
    }
    lpList[n] = NULL;

    static GUID guidSerial = {0x86E0D1E0L, 0x8089, 0x11D0, 0x9C, 0xE4, 0x08,
                              0x00,        0x3E,   0x30,   0x1F, 0x73};
    DWORD dwFlags = DIGCF_DEVICEINTERFACE;
    UINT mask = 0;
    if (lpPortExParams != NULL) {
        mask = lpPortExParams->mask;
    }
    if ((mask & SPPF_PRESENT) && lpPortExParams && lpPortExParams->bPresent) {
        dwFlags |= DIGCF_PRESENT;
    }
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&guidSerial, NULL, NULL, dwFlags);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        FreePortExList(lpList);
        return FALSE;
    }

    SP_DEVINFO_DATA devInfoData = {
        .cbSize = sizeof(SP_DEVINFO_DATA),
    };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        HKEY hDevKey = SetupDiOpenDevRegKey(
            hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hDevKey == INVALID_HANDLE_VALUE) {
            continue;
        }
        SPPORTEX portEx = {0};
        LONG ret =
            GetRegValueStr(hDevKey, TEXT("PortName"), &(portEx.pszPortName));
        RegCloseKey(hDevKey);
        if (ret != ERROR_SUCCESS) {
            continue;
        }
        DWORD dwInstallState;
        if (GetDeviceRegPropVar(hDevInfo, &devInfoData, SPDRP_INSTALL_STATE,
                                &dwInstallState)) {
            if ((mask & SPPF_PRESENT) && !(lpPortExParams->bPresent)) {
                free(portEx.pszPortName);
                continue;
            }
            portEx.bPresent = TRUE;
        } else if ((mask & SPPF_PRESENT) && (lpPortExParams->bPresent)) {
            free(portEx.pszPortName);
            continue;
        }

        LPSTR szHardwareId;
        if (GetDeviceRegPropStr(hDevInfo, &devInfoData, SPDRP_HARDWAREID,
                                &szHardwareId)) {
            DWORD dwVendorId;
            DWORD dwProductId;
            int ret = _stscanf(szHardwareId, TEXT("USB\\VID_%4x&PID_%4x"),
                               &dwVendorId, &dwProductId);
            free(szHardwareId);
            if (ret != -1) {
                if ((mask & SPPF_VENDORID) &&
                    (lpPortExParams->dwVendorId != dwVendorId)) {
                    free(portEx.pszPortName);
                    continue;
                }
                if ((mask & SPPF_PRODUCTID) &&
                    (lpPortExParams->dwProductId != dwProductId)) {
                    free(portEx.pszPortName);
                    continue;
                }
                portEx.dwVendorId = dwVendorId;
                portEx.dwProductId = dwProductId;
            } else {
                free(portEx.pszPortName);
                continue;
            }
        } else {
            free(portEx.pszPortName);
            continue;
        }

        if (!GetDeviceRegPropStr(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME,
                                 &(portEx.pszFriendlyName))) {
            free(portEx.pszPortName);
            continue;
        }

        lpList[n] = (LPSPPORTEX)malloc(sizeof(SPPORTEX));
        if (lpList[n] == NULL) {
            FreePortExList(lpList);
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return FALSE;
        }
        memcpy(lpList[n], &portEx, sizeof(SPPORTEX));
        n++;
        if (n > caps) {
            caps += CAPS;
            LPSPPORTEX *lpListNew =
                (LPSPPORTEX *)realloc(lpList, caps * sizeof(LPSPPORTEX));
            if (lpListNew == NULL) {
                FreePortExList(lpList);
                SetupDiDestroyDeviceInfoList(hDevInfo);
                return FALSE;
            }
            lpList = lpListNew;
        }
        lpList[n] = NULL;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    *lpPortExList = lpList;
    *lpExCount = n;

    return TRUE;
}

#pragma endregion Port Names

#pragma region Port Listner

/****************************************************************************
*
*     FUNCTION: ListnerProc
*
*     PURPOSE:  Monitor serial port and post event data to the main thread in
*                order to create event notifications.
*
*     PARAMS:   LPVOID StartParam - the class handle.
*
*     RETURNS:  DWORD - termination code.
*
* History:
*                September '08 - Created
*
\****************************************************************************/

DWORD WINAPI Listner_Proc(LPVOID StartParam)
{
    BOOL fStarting = TRUE;
    DWORD dwEvtMask = 0;
    OVERLAPPED ov;

    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ov.hEvent = g_lpInst->hListnerEvent; // May '11 added
    ResetEvent(ov.hEvent);               // May '11 added

    if (INVALID_HANDLE_VALUE != g_lpInst->hComm) {
        while (!g_lpInst->fEndListner) { // May '11 added
            // Specify the events and start the event thread
            if (SetCommMask(g_lpInst->hComm, EV_BREAK | EV_CTS | EV_DSR |
                                                 EV_ERR | EV_RING | EV_RLSD |
                                                 EV_RXCHAR | EV_RXFLAG)) {
                // Tells the main thread that this thread is ready for action.
                if (fStarting) {
                    SetEvent(g_lpInst->hStartEvent);
                    fStarting = FALSE;
                }
                if (!WaitCommEvent(g_lpInst->hComm, &dwEvtMask, &ov)) {
                    if (GetLastError() == ERROR_IO_PENDING) {
                        DWORD numBytes;
                        BOOL flag = WaitForSingleObject(ov.hEvent, -1);
                        do {
                            flag = GetOverlappedResult(g_lpInst->hComm, &ov,
                                                       &numBytes, FALSE);
                        } while ((GetLastError() == ERROR_IO_PENDING) && !flag);
                    }
                }
                PostMessage((HWND)StartParam, SPM_DISPATCHNOTIFICATIONS,
                            (DWORD)dwEvtMask, 0L);
            }
        }
    }
    return 0xDEAD;
}

/****************************************************************************
*
*     FUNCTION: LaunchListner
*
*     PURPOSE:  Initiates the event thread.
*
*     PARAMS:   HWND hwnd - the class handle.
*
*     RETURNS:  BOOL - true if successfull.
*
* History:
*                September '08 - Created
*
\****************************************************************************/

BOOL LaunchListner(HWND hwnd)
{
    if (NULL != g_lpInst->hEventThread) { // May '11 added
        CloseHandle(g_lpInst->hEventThread);
        g_lpInst->hEventThread = NULL;
        g_lpInst->dwEventThread = 0;
    }
    g_lpInst->fEndListner = FALSE;
    g_lpInst->hEventThread = CreateThread(NULL, 4095, Listner_Proc, hwnd, 0,
                                          &g_lpInst->dwEventThread);
    SetThreadPriority(g_lpInst->hEventThread, THREAD_PRIORITY_ABOVE_NORMAL);

    // More robust thread start-up wait.
    WaitForSingleObject(g_lpInst->hStartEvent, 500);

    return NULL != g_lpInst->hEventThread;
}

/****************************************************************************
*
*     FUNCTION: TerminateListner
*
*     PURPOSE:  Terminate the event thread.
*
*     COMMENTS: When WaitCommEvent() in WaitCommEventProc()
*                waits forever, it ties up the serial port object, and
*                consequently the main thread if it attempts to
*                access the object or object handle too.
*
*     RETURNS:  VOID.
*
* History:
*                September '08 - Created
*
\****************************************************************************/

VOID TerminateListner(VOID)
{
    // May '11 Rewrote to eliminate the call to TerminateThread()

    g_lpInst->fEndListner = TRUE;

    // Unblock WaitCommEvent()
    SetCommMask(g_lpInst->hComm, 0x00);
    EscapeCommFunction(g_lpInst->hComm, CLRDTR);

    // Clean up
    PurgeComm(g_lpInst->hComm, PURGE_RXCLEAR | PURGE_TXCLEAR);
    SetEvent(g_lpInst->hListnerEvent);
}

#pragma endregion Port Listner

#pragma region Message Handlers

/****************************************************************************
*
*     FUNCTION: SerialPort_OnCreate
*
*     PURPOSE:  WM_CREATE handler.
*
*     PARAMS:   HWND hwnd - control handle
*               LPCREATESTRUCT lpCreateStruct - pointer to the create struct
*
*     RETURNS:  BOOL - TRUE if successful
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct)
{
    INSTANCEDATA inst;

    inst.dwEventThread = 0;
    inst.dwPortCount = 0;
    inst.lpComm = NULL; // initialize the buffer pointer
    SetStringBuffer(&inst.lpComm, _T("COM1"));
    inst.flowControl = CtsRtsFlowControl;
    inst.hComm = INVALID_HANDLE_VALUE;
    inst.hEventThread = NULL;
    inst.dwReceiveByteThreshold = 1;
    inst.hwndParent = lpCreateStruct->hwndParent;
    inst.lpPortList = NULL;
    inst.lpPortExList = NULL;
    inst.hStartEvent =
        CreateEvent(NULL, TRUE, TRUE, NULL); // Manual reset events
    inst.hIOEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    inst.hListnerEvent =
        CreateEvent(NULL, FALSE, FALSE,
                    NULL); // new AutoResetEvent(false) = CreateEvent(NULL,
                           // FALSE, FALSE, NULL);    //AutoResetEvent;

    return Control_CreateInstanceData(hwnd, &inst);
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnDestroy
*
*     PURPOSE:  WM_DESTROY handler.
*
*     PARAMS:   HWND hwnd - control handle
*
*     RETURNS:  VOID
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

VOID SerialPort_OnDestroy(HWND hwnd)
{
    // Make sure the Port is closed and event thread is shut down
    SerialPort_Close(hwnd);

    // Free allocated storage
    if (NULL != g_lpInst->lpPortList) {
        FreePortNameList(g_lpInst->lpPortList);
    }

    if (NULL != g_lpInst->lpPortExList) {
        FreePortExList(g_lpInst->lpPortExList);
    }

    if (NULL != g_lpInst->lpComm) {
        free(g_lpInst->lpComm);
    }

    // Free resources
    CloseHandle(g_lpInst->hEventThread);  // May '11 Added
    CloseHandle(g_lpInst->hListnerEvent); // May '11 Added
    CloseHandle(g_lpInst->hStartEvent);
    CloseHandle(g_lpInst->hIOEvent);
    Control_FreeInstanceData(hwnd);
    PostQuitMessage(0);
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnSetConfig
*
*     PURPOSE:  SPM_SETCONFIG handler.
*
*     PARAMS:   LPCONFIG lpc - pointer to config struct
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnSetConfig(LPSPCONFIG lpc)
{
    BOOL fRtn = FALSE;

    // Desired options
    if (SPCF_PORTNAME & lpc->mask) {
        SetStringBuffer(&g_lpInst->lpComm, lpc->pszPortName);
        fRtn = TRUE;
    }
    if (g_lpInst->hComm == NULL || g_lpInst->hComm == INVALID_HANDLE_VALUE) {
        return fRtn;
    }

    DCB dcb;
    if (GetCommState(g_lpInst->hComm, &dcb)) {
        // Desired options (continued)
        if (SPCF_BAUDRATE & lpc->mask) {
            dcb.BaudRate = lpc->dwBaudRate;
        }
        if (SPCF_PARITY & lpc->mask) {
            dcb.Parity = lpc->bParity;
            dcb.fParity = dcb.Parity != NOPARITY;
        }
        if (SPCF_DATABITS & lpc->mask)
            dcb.ByteSize = lpc->bDataBits;
        if (SPCF_STOPBITS & lpc->mask)
            dcb.StopBits = lpc->bStopBits;
        if (SPCF_NULLDISCARD & lpc->mask)
            dcb.fNull = lpc->fDiscardNull;
        if (SPCF_FLOWCONT & lpc->mask) {
            g_lpInst->flowControl = lpc->flowControl;

            // Setup the flow control
            dcb.fDsrSensitivity = FALSE;
            dcb.fTXContinueOnXoff = FALSE;
            dcb.fRtsControl = RTS_CONTROL_DISABLE;
            dcb.fDtrControl = DTR_CONTROL_ENABLE;

            switch (g_lpInst->flowControl) {
            case NoFlowControl: {
                dcb.fOutxCtsFlow = FALSE;
                dcb.fOutxDsrFlow = FALSE;
                dcb.fOutX = FALSE;
                dcb.fInX = FALSE;
                break;
            }
            case CtsRtsFlowControl: {
                dcb.fOutxCtsFlow = TRUE;
                dcb.fOutxDsrFlow = FALSE;
                dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
                dcb.fOutX = FALSE;
                dcb.fInX = FALSE;
                break;
            }
            case CtsDtrFlowControl: {
                dcb.fOutxCtsFlow = TRUE;
                dcb.fOutxDsrFlow = FALSE;
                dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
                dcb.fOutX = FALSE;
                dcb.fInX = FALSE;
                break;
            }
            case DsrRtsFlowControl: {
                dcb.fOutxCtsFlow = FALSE;
                dcb.fOutxDsrFlow = TRUE;
                dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
                dcb.fOutX = FALSE;
                dcb.fInX = FALSE;
                break;
            }
            case DsrDtrFlowControl: {
                dcb.fOutxCtsFlow = FALSE;
                dcb.fOutxDsrFlow = TRUE;
                dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
                dcb.fOutX = FALSE;
                dcb.fInX = FALSE;
                break;
            }
            case XonXoffFlowControl: {
                dcb.fOutxCtsFlow = FALSE;
                dcb.fOutxDsrFlow = FALSE;
                dcb.fOutX = TRUE;
                dcb.fInX = TRUE;
                dcb.XonChar = 0x11;
                dcb.XoffChar = 0x13;
                dcb.XoffLim = 100;
                dcb.XonLim = 100;
                break;
            }
            }
        }
        fRtn = SetCommState(g_lpInst->hComm, &dcb);
    }
    return fRtn;
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnGetConfig
*
*     PURPOSE:  SPM_GETCONFIG handler.
*
*     PARAMS:   LPCONFIG lpc - pointer to config struct
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnGetConfig(LPSPCONFIG lpc)
{
    if (g_lpInst->hComm == NULL || g_lpInst->hComm == INVALID_HANDLE_VALUE)
        return FALSE;

    DCB dcb;
    if (!GetCommState(g_lpInst->hComm, &dcb))
        return FALSE;

    UINT mask = lpc->mask;
    memset(lpc, 0, sizeof(SPCONFIG));

    // Desired options
    lpc->mask = mask;
    if (SPCF_PORTNAME & mask) {
        lpc->pszPortName = g_lpInst->lpComm;
        lpc->cchTextMax = _tcslen(g_lpInst->lpComm) + 1;
    }
    if (SPCF_BAUDRATE & mask) {
        lpc->dwBaudRate = dcb.BaudRate;
    }
    if (SPCF_PARITY & mask) {
        lpc->bParity = dcb.Parity;
    }
    if (SPCF_DATABITS & mask) {
        lpc->bDataBits = dcb.ByteSize;
    }
    if (SPCF_STOPBITS & mask) {
        lpc->bStopBits = dcb.StopBits;
    }
    if (SPCF_NULLDISCARD & lpc->mask) {
        lpc->fDiscardNull = dcb.fNull;
    }
    if (SPCF_FLOWCONT & mask) {
        lpc->flowControl = g_lpInst->flowControl;
    }

    return TRUE;
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnSetReadTimout
*
*     PURPOSE:  SPM_SETREADTIMEOUT handler.
*
*     PARAMS:   DWORD dwTimeout - timeout in milliseconds or -1 (INFINATE)
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnSetReadTimout(DWORD dwTimeout)
{
    COMMTIMEOUTS aTimeout;
    memset(&aTimeout, 0, sizeof(aTimeout));
    if (!GetCommTimeouts(g_lpInst->hComm, &aTimeout)) {
        return FALSE;
    }
    if (0 == dwTimeout) {
        aTimeout.ReadTotalTimeoutConstant = 0;
        aTimeout.ReadTotalTimeoutMultiplier = 0;
        aTimeout.ReadIntervalTimeout = -1;
    } else if (-1 == dwTimeout) {
        aTimeout.ReadTotalTimeoutConstant = -2;
        aTimeout.ReadTotalTimeoutMultiplier = -1;
        aTimeout.ReadIntervalTimeout = -1;
    } else {
        aTimeout.ReadTotalTimeoutConstant = dwTimeout;
        aTimeout.ReadTotalTimeoutMultiplier = -1;
        aTimeout.ReadIntervalTimeout = -1;
    }
    return SetCommTimeouts(g_lpInst->hComm, &aTimeout);
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnSetWriteTimeout
*
*     PURPOSE:  SPM_SETWRITETIMEOUT handler.
*
*     PARAMS:   DWORD dwTimeout - timeout in milliseconds or -1 (INFINATE)
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnSetWriteTimeout(DWORD dwTimeout)
{
    COMMTIMEOUTS aTimeout;
    memset(&aTimeout, 0, sizeof(aTimeout));
    if (!GetCommTimeouts(g_lpInst->hComm, &aTimeout)) {
        return FALSE;
    }

    aTimeout.WriteTotalTimeoutConstant = dwTimeout == -1 ? 0 : dwTimeout;
    aTimeout.WriteTotalTimeoutMultiplier = 0;
    return SetCommTimeouts(g_lpInst->hComm, &aTimeout);
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnWriteBytes
*
*     PURPOSE:  SPM_WRITEBYTES handler.
*
*     PARAMS:   LPBYTE lpBuf - bytes to write
*               DWORD dwCount - number of bytes to write
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnWriteBytes(LPBYTE lpBuf, DWORD dwCount)
{
    BOOL fRtn = TRUE;
    DWORD actual;

    if (NULL == lpBuf) {
        fRtn = FALSE;
    } else {
        OVERLAPPED ov;
        memset(&ov, 0, sizeof(ov));
        ov.hEvent = g_lpInst->hIOEvent;

        if (!(fRtn =
                  WriteFile(g_lpInst->hComm, lpBuf, dwCount, &actual, &ov))) {
            if (GetLastError() != ERROR_IO_PENDING) {
                dwCount -= actual;
                actual = 0;
                GetOverlappedResult(g_lpInst->hComm, &ov, &actual, TRUE);
                dwCount -= actual;
                fRtn = dwCount == 0;
            }
        }
    }
    return fRtn;
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnWriteString
*
*     PURPOSE:  SPM_WRITESTRING handler.
*
*     PARAMS:   LPTSTR lpsztext - string to write
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnWriteString(LPTSTR lpsztext)
{
    BOOL fRtn = TRUE;
    DWORD actual;

    if (NULL == lpsztext) {
        fRtn = FALSE;
    } else {
        DWORD dwCount = _tcslen(lpsztext) + 1;
        OVERLAPPED ov;
        memset(&ov, 0, sizeof(ov));
        ov.hEvent = g_lpInst->hIOEvent;

        if (!(fRtn = WriteFile(g_lpInst->hComm, lpsztext, dwCount, &actual,
                               &ov))) {
            if (GetLastError() != ERROR_IO_PENDING) {
                dwCount -= actual;
                actual = 0;
                GetOverlappedResult(g_lpInst->hComm, &ov, &actual, TRUE);
                dwCount -= actual;
                fRtn = dwCount == 0;
            }
        }
    }
    return fRtn;
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnReadBytes
*
*     PURPOSE:  SPM_READBYTES handler.
*
*     PARAMS:   LPBYTE lpBuf - buffer to receive bytes
*               DWORD dwCount - buffer size
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

DWORD SerialPort_OnReadBytes(LPBYTE lpBuf, DWORD dwCount)
{
    DWORD actual;

    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = g_lpInst->hIOEvent;

    if (!ReadFile(g_lpInst->hComm, lpBuf, dwCount, &actual, &ov)) {
        actual = 0;
    }

    return actual;
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnReadString
*
*     PURPOSE:  SPM_READSTRING handler.
*
*     PARAMS:   LPTSTR lpszBuf - buffer to receive string
*               DWORD dwCount - buffer size
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

DWORD SerialPort_OnReadString(LPTSTR lpszBuf, DWORD dwCount)
{
    DWORD actual;

    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = g_lpInst->hIOEvent;

    if (!ReadFile(g_lpInst->hComm, lpszBuf, dwCount, &actual, &ov)) {
        actual = 0;
    }

    return actual;
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnClose
*
*     PURPOSE:  SPM_CLOSE handler.
*
*     PARAMS:   HWND hwnd - the handle of serial port class
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnClose(VOID)
{
    if (g_lpInst->hComm != NULL &&
        g_lpInst->hComm != INVALID_HANDLE_VALUE) { // May '11 changed || to &&
        TerminateListner();
        CloseHandle(g_lpInst->hComm);
        g_lpInst->hComm = INVALID_HANDLE_VALUE;
        return TRUE;
    }
    return FALSE;
}

/****************************************************************************
*
*     FUNCTION: SerialPort_OnOpen
*
*     PURPOSE:  SPM_OPEN handler.
*
*     PARAMS:   HWND hwnd - the handle of serial port class
*               LPCONFIG lpc - pointer to config struct
*
*     RETURNS:  TRUE if successfull
*
* History:
*                Oct '09 - Created
*
\****************************************************************************/

BOOL SerialPort_OnOpen(HWND hwnd, LPSPCONFIG lpc)
{
    SerialPort_OnClose();

    if (SPCF_PORTNAME & lpc->mask) {
        SetStringBuffer(&g_lpInst->lpComm, lpc->pszPortName);
        lpc->mask ^= SPCF_PORTNAME; // Now that we handled this, switch it out
    }
    TCHAR buf[MAX_PATH];
    LPTSTR format;

#ifdef _UNICODE
    format = _T("\\\\.\\%ls");
#else
    format = _T("\\\\.\\%s");
#endif
    _sntprintf(buf, MAX_PATH, format, g_lpInst->lpComm);

    // Call CreateFile to open the comms port
    g_lpInst->hComm = CreateFile(buf, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                 OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (g_lpInst->hComm == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    if (!LaunchListner(hwnd)) {
        return FALSE;
    }

    // Defaults
    if (!(SPCF_BAUDRATE & lpc->mask)) {
        lpc->mask |= SPCF_BAUDRATE;
        lpc->dwBaudRate = CBR_9600;
    }
    if (!(SPCF_PARITY & lpc->mask)) {
        lpc->mask |= SPCF_PARITY;
        lpc->bParity = NOPARITY;
    }
    if (!(SPCF_DATABITS & lpc->mask)) {
        lpc->mask |= SPCF_DATABITS;
        lpc->bDataBits = 8;
    }
    if (!(SPCF_STOPBITS & lpc->mask)) {
        lpc->mask |= SPCF_STOPBITS;
        lpc->bStopBits = ONESTOPBIT;
    }
    if (!(SPCF_NULLDISCARD & lpc->mask)) {
        lpc->mask |= SPCF_NULLDISCARD;
        lpc->fDiscardNull = FALSE;
    }
    if (!(SPCF_FLOWCONT & lpc->mask)) {
        lpc->mask |= SPCF_FLOWCONT;
        lpc->flowControl = CtsRtsFlowControl;
    }

    return SerialPort_OnSetConfig(lpc);
}

#pragma endregion Message Handlers

#pragma region Procs

/****************************************************************************
*
*     FUNCTION: SerialPort_NotifyDispatcher
*
*     PURPOSE:  Dispatch notifications for various serial port events.
*
*     PARAMS:   HWND hwnd - the class handle.
*               DWORD dwEvtMask - notification event mask.
*
*     RETURNS:  DWORD
*
* History:
*                September '08 - Created
*
\****************************************************************************/

VOID SerialPort_NotifyDispatcher(HWND hwnd, DWORD dwEvtMask)
{
    static NMHDR nmhdr;
    static SPNMSERIAL nmSerial;

    nmhdr.hwndFrom = hwnd;
    nmhdr.idFrom = GetDlgCtrlID(hwnd);
    nmSerial.hdr = nmhdr;

    if (dwEvtMask & EV_ERR) {
        DWORD dwErrors;

        if (ClearCommError(g_lpInst->hComm, &dwErrors, NULL)) {
            nmSerial.hdr.code = SPN_ERRORRECEIVED;

            if (dwErrors & CE_TXFULL) {
                // The application tried to transmit a
                // character, but
                // the output buffer was full.
                nmSerial.dwCode = CE_TXFULL;
                FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                                  SNDMSG);
            }
            if (dwErrors & CE_RXOVER) {
                // An input buffer overflow has occurred.
                // There is either no room in the input
                // buffer, or a character was received
                // after the end-of-file (EOF) character.
                nmSerial.dwCode = CE_RXOVER;
                FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                                  SNDMSG);
            }
            if (dwErrors & CE_OVERRUN) {
                // A character-buffer overrun has
                // occurred. The next character is lost.
                nmSerial.dwCode = CE_OVERRUN;
                FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                                  SNDMSG);
            }
            if (dwErrors & CE_RXPARITY) {
                // The hardware detected a parity error.
                nmSerial.dwCode = CE_RXPARITY;
                FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                                  SNDMSG);
            }
            if (dwErrors & CE_FRAME) {
                // The hardware detected a framing error.
                nmSerial.dwCode = CE_FRAME;
                FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                                  SNDMSG);
            }
        }
    }
    if (dwEvtMask & (EV_CTS | EV_DSR | EV_RLSD | EV_BREAK | EV_RING)) {
        nmSerial.hdr.code = SPN_PINCHANGED;

        if (dwEvtMask & EV_CTS) {
            // The Clear to Send (CTS) signal changed state.
            // This signal is used to indicate whether data
            // can be sent over the serial port.
            nmSerial.dwCode = EV_CTS;
            FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                              SNDMSG);
        }
        if (dwEvtMask & EV_DSR) {
            // The Data Set Ready (DSR) signal changed state. This
            // signal is used to indicate whether the device on the
            // serial port is ready to operate.
            nmSerial.dwCode = EV_DSR;
            FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                              SNDMSG);
        }
        if (dwEvtMask & EV_RLSD) {
            // The Carrier Detect (CD) signal changed state. This
            // signal is used to indicate whether a modem is connected
            // to a working phone line and a data carrier signal is
            // detected.
            nmSerial.dwCode = EV_RLSD;
            FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                              SNDMSG);
        }
        if (dwEvtMask & EV_RING) {
            // A ring indicator was detected.
            nmSerial.dwCode = EV_RING;
            FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                              SNDMSG);
        }
        if (dwEvtMask & EV_BREAK) {
            // A break was detected on input.
            nmSerial.dwCode = EV_BREAK;
            FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                              SNDMSG);
        }
    }
    if (dwEvtMask & (EV_RXCHAR | EV_RXFLAG)) {
        nmSerial.hdr.code = SPN_DATARECEIVED;

        if (dwEvtMask & EV_RXCHAR) {
            // A character was received and placed in
            // the input buffer.
            if (0 < g_lpInst->dwReceiveByteThreshold &&
                g_lpInst->dwReceiveByteThreshold <=
                    SerialPort_BytesToRead(hwnd)) {
                nmSerial.dwCode = EV_RXCHAR;
                FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                                  SNDMSG);
            }
        }
        if (dwEvtMask & EV_RXFLAG) {
            // The end of file character was received
            // and placed in the input buffer.
            nmSerial.dwCode = EV_RXFLAG;
            FORWARD_WM_NOTIFY(g_lpInst->hwndParent, nmhdr.idFrom, &nmSerial,
                              SNDMSG);
        }
    }
}

/****************************************************************************
*
*     FUNCTION: SerialPort_Proc
*
*     PURPOSE:  Public interface for the serial port control.
*
*     PARAMS:   HWND   hwnd     - the class handle
*               UINT   msg      - which message?
*               WPARAM wParam   - message parameter
*               LPARAM lParam   - message parameter
*
*     RETURNS:  LRESULT   - depends on message
*
* History:
*                March '08 - Created
*
\****************************************************************************/

LRESULT CALLBACK SerialPort_Proc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam)
{
    if (SPM_DISPATCHNOTIFICATIONS <= msg && msg <= SPM_GETPORTSEX) {
        Control_GetInstanceData(hwnd, &g_lpInst); // Update instance pointer
    }
    switch (msg) {
        HANDLE_MSG(hwnd, WM_CREATE, SerialPort_OnCreate);
        HANDLE_MSG(hwnd, WM_DESTROY, SerialPort_OnDestroy);

    case SPM_DISPATCHNOTIFICATIONS:
        SerialPort_NotifyDispatcher(hwnd, (DWORD)wParam);
        break;

    case SPM_GETPORTNAMES:
        if (NULL != g_lpInst->lpPortList) {
            FreePortNameList(g_lpInst->lpPortList);
            g_lpInst->dwPortCount = 0;
        }

        if (ERROR_SUCCESS ==
            GetPortNames(&(g_lpInst->lpPortList), &(g_lpInst->dwPortCount))) {
            *((LPDWORD)wParam) = g_lpInst->dwPortCount;
            return (LRESULT)g_lpInst->lpPortList;
        }
        return (LRESULT)NULL;

    case SPM_SETCONFIG:
        return SerialPort_OnSetConfig((LPSPCONFIG)lParam);

    case SPM_GETCONFIG:
        return SerialPort_OnGetConfig((LPSPCONFIG)lParam);

    case SPM_SETREADTIMEOUT:
        return SerialPort_OnSetReadTimout((DWORD)wParam);

    case SPM_GETREADTIMEOUT: {
        COMMTIMEOUTS aTimeout;
        memset(&aTimeout, 0, sizeof(aTimeout));
        if (!GetCommTimeouts(g_lpInst->hComm, &aTimeout))
            return FALSE;
        return (LRESULT)aTimeout.ReadTotalTimeoutConstant == -2
                   ? -1
                   : aTimeout.ReadTotalTimeoutConstant;
    }

    case SPM_SETWRITETIMEOUT:
        return SerialPort_OnSetWriteTimeout((DWORD)wParam);

    case SPM_GETWRITETIMEOUT: {
        COMMTIMEOUTS aTimeout;
        memset(&aTimeout, 0, sizeof(aTimeout));
        if (!GetCommTimeouts(g_lpInst->hComm, &aTimeout))
            return FALSE;
        return (LRESULT)aTimeout.WriteTotalTimeoutConstant;
    }

    case SPM_GETCTS: {
        DWORD status;
        if (!GetCommModemStatus(g_lpInst->hComm, &status))
            return -1;
        return (LRESULT)status & MS_CTS_ON;
    }

    case SPM_GETDSR: {
        DWORD status;
        if (!GetCommModemStatus(g_lpInst->hComm, &status))
            return -1;
        return (LRESULT)status & MS_DSR_ON;
    }

    case SPM_BYTESTOREAD: {
        DWORD status;
        COMSTAT comStat;

        ClearCommError(g_lpInst->hComm, &status, &comStat);

        return (LRESULT)comStat.cbInQue;
    }

    case SPM_BYTESTOWRITE: {
        DWORD status;
        COMSTAT comStat;
        ClearCommError(g_lpInst->hComm, &status, &comStat);

        return (LRESULT)comStat.cbOutQue;
    }

    case SPM_WRITEBYTES:
        return SerialPort_OnWriteBytes((LPBYTE)lParam, (DWORD)wParam);

    case SPM_READBYTES:
        return SerialPort_OnReadBytes((LPBYTE)lParam, (DWORD)wParam);

    case SPM_WRITESTRING:
        return SerialPort_OnWriteString((LPTSTR)lParam);

    case SPM_READSTRING:
        return SerialPort_OnReadString((LPTSTR)lParam, (DWORD)wParam);

    case SPM_CLOSE:
        return SerialPort_OnClose();

    case SPM_OPEN:
        return SerialPort_OnOpen(hwnd, (LPSPCONFIG)lParam);

    case SPM_FLUSH:
        return PurgeComm(g_lpInst->hComm, (DWORD)wParam);

    case SPM_SETRXTHRESHOLD:
        g_lpInst->dwReceiveByteThreshold = (DWORD)wParam;
        return TRUE;

    case SPM_GETRXTHRESHOLD:
        return (LRESULT)g_lpInst->dwReceiveByteThreshold;

    case SPM_GETPORTSEX:
        if (NULL != g_lpInst->lpPortExList) {
            FreePortExList(g_lpInst->lpPortExList);
            g_lpInst->dwPortExCount = 0;
        }

        if (GetPortsEx(&(g_lpInst->lpPortExList), (LPSPPORTEXPARAMS)lParam,
                       &(g_lpInst->dwPortExCount))) {
            *((LPDWORD)wParam) = g_lpInst->dwPortExCount;
            return (LRESULT)g_lpInst->lpPortExList;
        }
        return (LRESULT)NULL;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

#pragma endregion Procs

/****************************************************************************
*
*     FUNCTION: InitSerialCommPort
*
*     PURPOSE:  Initialize and register the custom control
*
*     PARAMS:   HINSTANCE hInstance - handle of application instance
*
*     RETURNS:  BOOL TRUE if successfull
*
* History:
*                Jan '08 - Created
*
\****************************************************************************/

ATOM InitSerialPortControl(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    // Register this class
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_BYTEALIGNCLIENT;
    wcex.lpfnWndProc = (WNDPROC)SerialPort_Proc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hCursor = NULL;
    wcex.hbrBackground = NULL;
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = g_szClassName;
    wcex.hIcon = NULL;
    wcex.hIconSm = NULL;

    return RegisterClassEx(&wcex);
}

/****************************************************************************
*
*     FUNCTION: New_SerialPortControl
*
*     PURPOSE:  Create an new instance of the control
*
*     PARAMS:   HWND hParent
*               DWORD dwID
*
*     RETURNS:  HWND handle to a dialog if successfull else NULL
*
* History:
*                March '08 - Created
*
\***************************************************************************/

HWND New_SerialPortControl(HWND hParent, DWORD dwID)
{
    static ATOM aSerialPortControl = 0;
    static HWND hSerial;

    HINSTANCE hinst = (HINSTANCE)GetWindowLongPtr(hParent, GWLP_HINSTANCE);

    // Only need to register serial control once
    if (!aSerialPortControl)
        aSerialPortControl = InitSerialPortControl(hinst);

    hSerial = CreateWindowEx(0, g_szClassName, _T(""), WS_CHILD, 0, 0, 0, 0,
                             hParent, (HMENU)dwID, hinst, NULL);

    return hSerial;
}
