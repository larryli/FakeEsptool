/*
 * app_commands.c - Command handler functions
 *
 * Handles menu commands, toolbar actions, and UI state updates.
 */

#include "app_commands.h"
#include "app_logview.h"
#include "main.h"
#include "resource.h"
#include "esptool/device.h"
#include "esptool/chip.h"
#include <richedit.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#if ENABLE_TRACE
static const char *TAG = "CMD";
#endif

/* Helper: Check if serial is connected, prompt to disconnect if needed */
BOOL PromptDisconnectIfNeeded(HWND hWnd)
{
    if (!Serial_IsOpen(&g_serial))
        return TRUE;

    int ret = MessageBoxW(hWnd,
                          L"Serial port is connected.\nDo you want to disconnect?",
                          L"Disconnect",
                          MB_YESNO | MB_ICONQUESTION);
    if (ret != IDYES)
        return FALSE;

    Serial_Close(&g_serial);
    UpdateTitle(hWnd);
    UpdateMenuState(hWnd);
    UpdateStatusBar();
    return TRUE;
}

/* Helper: Check if device is modified, prompt to save if needed */
BOOL PromptSaveIfNeeded(HWND hWnd)
{
    if (!Device_IsModified(&g_device))
        return TRUE;

    int ret = MessageBoxW(hWnd,
                          L"Device data has been modified.\nDo you want to save changes?",
                          L"Save Changes",
                          MB_YESNOCANCEL | MB_ICONQUESTION);
    switch (ret) {
    case IDYES:
        {
            const WCHAR *filename = Device_GetFilename(&g_device);
            if (filename[0]) {
                Device_Save(&g_device, filename);
            } else {
                OPENFILENAMEW ofn = {0};
                WCHAR szFile[MAX_PATH] = {0};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFilter = LoadStr(IDS_DEVICE_SAVE_FILTER);
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                ofn.lpstrDefExt = L"esp";
                if (!GetSaveFileNameW(&ofn))
                    return FALSE;
                Device_Save(&g_device, szFile);
            }
        }
        return TRUE;
    case IDNO:
        return TRUE;
    default:
        return FALSE;
    }
}

/* Helper: Check if a specific port exists in the system */
BOOL IsPortAvailable(const WCHAR *portName)
{
    WCHAR fullPort[32];
    wsprintfW(fullPort, L"\\\\.\\%s", portName);
    HANDLE hPort = CreateFileW(fullPort, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hPort == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(hPort);
    return TRUE;
}

/* Helper: Check if reconnect is available (last port exists and not connected) */
BOOL CanReconnect(void)
{
    if (Serial_IsOpen(&g_serial))
        return FALSE;
    if (!g_szPort[0])
        return FALSE;
    return IsPortAvailable(g_szPort);
}

/* Update menu and toolbar button states based on connection status */
void UpdateMenuState(HWND hWnd)
{
    HMENU hMenu = GetMenu(hWnd);
    BOOL connected = Serial_IsOpen(&g_serial);
    BOOL canReconnect = CanReconnect();

    EnableMenuItem(hMenu, IDM_CONNECT, connected ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, IDM_DISCONNECT, connected ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_RECONNECT, canReconnect ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_DEVICE_PROPS, connected ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, IDM_FLASH_IMPORT, connected ? MF_GRAYED : MF_ENABLED);

    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_CONNECT, !connected);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_DISCONNECT, connected);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_RECONNECT, canReconnect);
}

/* Update window title: FakeEsptool - [Chip] - [File][*] - [Port] */
void UpdateTitle(HWND hWnd)
{
    WCHAR title[256] = {0};
    WCHAR *p = title;

    /* Base name */
    lstrcpyW(title, L"FakeEsptool");
    p = title + lstrlenW(title);

    /* Chip type */
    if (g_device.chip.name[0]) {
        WCHAR chipName[32];
        MultiByteToWideChar(CP_UTF8, 0, g_device.chip.name, -1, chipName, 32);
        wsprintfW(p, L" - %s", chipName);
        p += lstrlenW(p);
    }

    /* File name */
    const WCHAR *filename = Device_GetFilename(&g_device);
    if (filename[0]) {
        /* Extract filename without path */
        const WCHAR *name = wcsrchr(filename, L'\\');
        name = name ? name + 1 : filename;
        wsprintfW(p, L" - %s", name);
    } else {
        lstrcatW(p, L" - Untitled");
    }
    p += lstrlenW(p);

    /* Modified indicator */
    if (Device_IsModified(&g_device)) {
        lstrcatW(p, L"*");
        p++;
    }

    /* Serial port */
    if (Serial_IsOpen(&g_serial)) {
        wsprintfW(p, L" - %s", g_szPort);
    }

    SetWindowTextW(hWnd, title);
}

/* Update status bar: [Chip] [Flash] [MAC] [Port] [Config] */
void UpdateStatusBar(void)
{
    if (!g_hStatusbar)
        return;

    int parts[5];
    RECT rc;
    GetClientRect(GetParent(g_hStatusbar), &rc);
    parts[0] = STATUS_PART1_WIDTH;
    parts[1] = parts[0] + STATUS_PART2_WIDTH;
    parts[2] = parts[1] + STATUS_PART3_WIDTH;
    parts[3] = parts[2] + STATUS_PART4_WIDTH;
    parts[4] = -1;
    SendMessageW(g_hStatusbar, SB_SETPARTS, 5, (LPARAM)parts);

    /* Part 1: Chip type */
    if (g_device.chip.name[0]) {
        WCHAR chipName[32];
        MultiByteToWideChar(CP_UTF8, 0, g_device.chip.name, -1, chipName, 32);
        SendMessageW(g_hStatusbar, SB_SETTEXT, 0, (LPARAM)chipName);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 0, (LPARAM)L"No Device");
    }

    /* Part 2: Flash size */
    if (g_device.flash.size > 0) {
        WCHAR flashSize[32];
        if (g_device.flash.size >= 1024*1024)
            wsprintfW(flashSize, L"%luMB", g_device.flash.size / (1024*1024));
        else
            wsprintfW(flashSize, L"%luKB", g_device.flash.size / 1024);
        SendMessageW(g_hStatusbar, SB_SETTEXT, 1, (LPARAM)flashSize);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 1, (LPARAM)L"");
    }

    /* Part 3: MAC address */
    if (g_device.chip.name[0]) {
        const BYTE *mac = Chip_GetMac(&g_device.chip);
        WCHAR macStr[32];
        wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        SendMessageW(g_hStatusbar, SB_SETTEXT, 2, (LPARAM)macStr);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 2, (LPARAM)L"");
    }

    /* Part 4 & 5: Serial port and config */
    if (Serial_IsOpen(&g_serial)) {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 3, (LPARAM)g_szPort);

        DWORD baudRate = 115200;
        BYTE dataBits = 8, parity = NOPARITY, stopBits = ONESTOPBIT;
        Serial_GetConfig(&g_serial, &baudRate, &dataBits, &parity, &stopBits);

        const WCHAR *parityStr = L"N";
        switch (parity) {
        case NOPARITY: parityStr = L"N"; break;
        case ODDPARITY: parityStr = L"O"; break;
        case EVENPARITY: parityStr = L"E"; break;
        case MARKPARITY: parityStr = L"M"; break;
        case SPACEPARITY: parityStr = L"S"; break;
        }

        const WCHAR *stopStr = L"1";
        switch (stopBits) {
        case ONESTOPBIT: stopStr = L"1"; break;
        case ONE5STOPBITS: stopStr = L"1.5"; break;
        case TWOSTOPBITS: stopStr = L"2"; break;
        }

        WCHAR configBuf[32];
        wsprintfW(configBuf, L"%lu,%d%s%s", baudRate, dataBits, parityStr, stopStr);
        SendMessageW(g_hStatusbar, SB_SETTEXT, 4, (LPARAM)configBuf);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 3, (LPARAM)LoadStr(IDS_DISCONNECTED));
        SendMessageW(g_hStatusbar, SB_SETTEXT, 4, (LPARAM)L"");
    }
}

/* Apply font to RichEdit control */
void ApplyFontToEdit(HWND hEdit, LOGFONTW *plf)
{
    CHARFORMAT2W cf = {0};
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT;
    cf.yHeight = plf->lfHeight * 15;  /* Convert to twips (1/1440 inch) */
    cf.wWeight = (WORD)plf->lfWeight;
    lstrcpyW(cf.szFaceName, plf->lfFaceName);
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

/* Initialize default font (try to load from config) */
void InitDefaultFont(void)
{
    /* Try to load from config first */
    if (!Config_GetFont(&g_logFont)) {
        /* Use default if not in config */
        HDC hdc = GetDC(NULL);
        g_logFont.lfHeight = -MulDiv(DEFAULT_FONT_SIZE, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(NULL, hdc);
        g_logFont.lfWeight = FW_NORMAL;
        g_logFont.lfCharSet = DEFAULT_CHARSET;
        g_logFont.lfOutPrecision = OUT_TT_PRECIS;
        g_logFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        g_logFont.lfQuality = CLEARTYPE_QUALITY;
        g_logFont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        lstrcpyW(g_logFont.lfFaceName, DEFAULT_FONT_NAME);
    }
}

/* Handle IDM_NEW_DEVICE command */
void Main_CmdNewDevice(HWND hWnd)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return;
    if (!PromptSaveIfNeeded(hWnd))
        return;

    /* Create default device: ESP32, 40MHz, 4MB */
    static const BYTE defaultMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    Device_Close(&g_device);
    if (Device_Init(&g_device, CHIP_ESP32, 4 * 1024 * 1024, defaultMac)) {
        g_device.chip.xtal_freq = XTAL_FREQ_40M;
        Esptool_SetModifiedCallback(&g_esptool, OnDeviceModified);
        Config_SetLastDeviceFile(NULL);
        UpdateStatusBar();
        UpdateTitle(hWnd);
        SetWindowTextW(g_hEdit, L"");
    } else {
        MessageBoxW(hWnd, L"Failed to create device", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
    }
}

/* Handle IDM_OPEN_DEVICE command */
void Main_CmdOpenDevice(HWND hWnd)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return;
    if (!PromptSaveIfNeeded(hWnd))
        return;
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = LoadStr(IDS_DEVICE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        Device_Close(&g_device);
        if (Device_Load(&g_device, szFile)) {
            Esptool_SetModifiedCallback(&g_esptool, OnDeviceModified);
            Config_SetLastDeviceFile(szFile);
            UpdateStatusBar();
            UpdateTitle(hWnd);
            SetWindowTextW(g_hEdit, L"");
        } else {
            MessageBoxW(hWnd, L"Failed to load device file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        }
    }
}

/* Open device file by path (used by command line and drag-drop) */
BOOL Main_OpenDeviceFile(HWND hWnd, const WCHAR *filePath)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return FALSE;
    if (!PromptSaveIfNeeded(hWnd))
        return FALSE;

    Device_Close(&g_device);
    if (Device_Load(&g_device, filePath)) {
        Esptool_SetModifiedCallback(&g_esptool, OnDeviceModified);
        Config_SetLastDeviceFile(filePath);
        UpdateStatusBar();
        UpdateTitle(hWnd);
        SetWindowTextW(g_hEdit, L"");
        return TRUE;
    } else {
        MessageBoxW(hWnd, L"Failed to load device file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return FALSE;
    }
}

/* Handle IDM_SAVE_DEVICE command */
void Main_CmdSaveDevice(HWND hWnd)
{
    const WCHAR *filename = Device_GetFilename(&g_device);
    if (filename[0]) {
        if (Device_Save(&g_device, filename)) {
            Config_SetLastDeviceFile(filename);
        }
    } else {
        /* No filename, do Save As */
        Main_CmdSaveDeviceAs(hWnd);
    }
}

/* Handle IDM_SAVE_DEVICE_AS command */
void Main_CmdSaveDeviceAs(HWND hWnd)
{
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = LoadStr(IDS_DEVICE_SAVE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"esp";
    if (GetSaveFileNameW(&ofn)) {
        if (Device_Save(&g_device, szFile)) {
            Config_SetLastDeviceFile(szFile);
        } else {
            MessageBoxW(hWnd, L"Failed to save device file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        }
    }
}

/* Handle IDM_DEVICE_PROPS command */
void Main_CmdDeviceProps(HWND hWnd)
{
    if (Serial_IsOpen(&g_serial)) {
        MessageBoxW(hWnd, L"Disconnect serial port before modifying device properties", LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
    } else {
        if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_DEVICE_PROPS), hWnd, DevicePropsDlgProc) == IDOK) {
            UpdateStatusBar();
            UpdateTitle(hWnd);
        }
    }
}

/* Handle Connect command */
void Main_OnConnect(HWND hMainWnd)
{
    TRACE_FW(TAG, "Main_OnConnect called");

    if (Serial_IsOpen(&g_serial)) {
        TRACE_FW(TAG, "Port already open");
        return;
    }

    if (!ShowPortSelectDialog(hMainWnd)) {
        TRACE_FW(TAG, "Port selection cancelled");
        return;
    }

    lstrcpyW(g_szPort, g_szSelectedPort);
    TRACE_FW(TAG, "Selected port: %s", g_szPort);
    TRACE_FW(TAG, "Calling Serial_Open...");

    if (!Serial_Open(&g_serial, g_szPort, hMainWnd)) {
        TRACE_FW(TAG, "ERROR: Serial_Open failed");
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_PORT_ERROR), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    Serial_SetReceiveCallback(&g_serial, (SERIAL_RX_CB)OnEsptoolProcessData);
    Serial_SetSignalCallback(&g_serial, (SERIAL_SIGNAL_CB)OnEsptoolSignal);
    ResetSignalState();

    TRACE_FW(TAG, "Serial_Open succeeded");

    /* Save last connected port */
    Config_SetLastPort(g_szPort);

    /* Clear log on new connection */
    SetWindowTextW(g_hEdit, L"");

    UpdateTitle(hMainWnd);
    UpdateMenuState(hMainWnd);
    UpdateStatusBar();
    SetFocus(g_hEdit);

    TRACE_FW(TAG, "Main_OnConnect completed");
}

/* Handle Reconnect command - connect to last port directly */
void Main_OnReconnect(HWND hMainWnd)
{
    TRACE_FW(TAG, "Main_OnReconnect called");

    if (Serial_IsOpen(&g_serial)) {
        TRACE_FW(TAG, "Port already open");
        return;
    }

    if (!g_szPort[0]) {
        MessageBoxW(hMainWnd, L"No last port available", LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
        return;
    }

    if (!IsPortAvailable(g_szPort)) {
        MessageBoxW(hMainWnd, L"Port is not available", LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
        return;
    }

    TRACE_FW(TAG, "Reconnecting to port: %s", g_szPort);

    if (!Serial_Open(&g_serial, g_szPort, hMainWnd)) {
        TRACE_FW(TAG, "ERROR: Serial_Open failed");
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_PORT_ERROR), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    /* Register esptool protocol callbacks */
    Serial_SetReceiveCallback(&g_serial, (SERIAL_RX_CB)OnEsptoolProcessData);
    Serial_SetSignalCallback(&g_serial, (SERIAL_SIGNAL_CB)OnEsptoolSignal);
    ResetSignalState();

    TRACE_FW(TAG, "Serial_Open succeeded");

    /* Clear log on new connection */
    SetWindowTextW(g_hEdit, L"");

    UpdateTitle(hMainWnd);
    UpdateMenuState(hMainWnd);
    UpdateStatusBar();
    SetFocus(g_hEdit);

    TRACE_FW(TAG, "Main_OnReconnect completed");
}

/* Handle Disconnect command */
void Main_OnDisconnect(HWND hMainWnd)
{
    if (!Serial_IsOpen(&g_serial))
        return;

    Serial_Close(&g_serial);

    UpdateTitle(hMainWnd);
    UpdateMenuState(hMainWnd);
    UpdateStatusBar();
    SetFocus(g_hEdit);
}

/* Handle Flash > Import command */
void Main_OnFlashImport(HWND hMainWnd)
{
    if (Serial_IsOpen(&g_serial)) {
        MessageBoxW(hMainWnd, L"Disconnect serial port before importing Flash", LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
        return;
    }

    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = LoadStr(IDS_BIN_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn))
        return;

    /* Check file size */
    HANDLE hFile = CreateFileW(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hMainWnd, L"Failed to open file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize != g_device.flash.size) {
        CloseHandle(hFile);
        WCHAR msg[128];
        wsprintfW(msg, L"File size (%lu bytes) does not match Flash size (%lu bytes)", fileSize, g_device.flash.size);
        MessageBoxW(hMainWnd, msg, LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    /* Show busy cursor and disable window */
    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    DWORD bytesRead;
    BOOL ok = ReadFile(hFile, g_device.flash.data, fileSize, &bytesRead, NULL) && bytesRead == fileSize;
    CloseHandle(hFile);

    /* Restore window state */
    EnableWindow(hMainWnd, TRUE);
    SetCursor(hOldCursor);

    if (!ok) {
        MessageBoxW(hMainWnd, L"Failed to read file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    Device_SetModified(&g_device, TRUE);
    UpdateTitle(hMainWnd);
}

/* Handle Flash > Export command */
void Main_OnFlashExport(HWND hMainWnd)
{
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = LoadStr(IDS_BIN_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"bin";

    if (!GetSaveFileNameW(&ofn))
        return;

    /* Create snapshot of flash data */
    DWORD flashSize = g_device.flash.size;
    BYTE *flashSnapshot = NULL;
    if (flashSize > 0 && g_device.flash.data) {
        flashSnapshot = (BYTE *)HeapAlloc(GetProcessHeap(), 0, flashSize);
        if (!flashSnapshot) {
            MessageBoxW(hMainWnd, L"Failed to allocate memory for snapshot", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
            return;
        }
        memcpy(flashSnapshot, g_device.flash.data, flashSize);
    }

    /* Show busy cursor and disable window */
    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    /* Write snapshot to file */
    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        EnableWindow(hMainWnd, TRUE);
        SetCursor(hOldCursor);
        HeapFree(GetProcessHeap(), 0, flashSnapshot);
        MessageBoxW(hMainWnd, L"Failed to create file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD bytesWritten;
    BOOL ok = TRUE;
    if (flashSnapshot && flashSize > 0) {
        ok = WriteFile(hFile, flashSnapshot, flashSize, &bytesWritten, NULL) && bytesWritten == flashSize;
    }
    CloseHandle(hFile);

    /* Restore window state */
    EnableWindow(hMainWnd, TRUE);
    SetCursor(hOldCursor);

    if (!ok) {
        DeleteFileW(szFile);
        HeapFree(GetProcessHeap(), 0, flashSnapshot);
        MessageBoxW(hMainWnd, L"Failed to write file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    HeapFree(GetProcessHeap(), 0, flashSnapshot);
}

/* Device snapshot structure for Dump Device As */
typedef struct {
    DEVICE_CTX device;      /* Device header info */
    BYTE *efuse;            /* eFuse data snapshot */
    DWORD efuseSize;        /* eFuse size */
    BYTE *flash;            /* Flash data snapshot */
    DWORD flashSize;        /* Flash size */
    WCHAR filename[MAX_PATH]; /* Output filename */
    HWND hWnd;              /* Owner window */
} DEVICE_SNAPSHOT;

/* Dump thread procedure */
static DWORD WINAPI DumpThreadProc(LPVOID lpParam)
{
    DEVICE_SNAPSHOT *snap = (DEVICE_SNAPSHOT *)lpParam;
    BOOL ok = TRUE;
    FILE *f = NULL;

    /* Open output file with UTF-8 encoding */
    f = _wfopen(snap->filename, L"w, ccs=UTF-8");
    if (!f) {
        ok = FALSE;
        goto cleanup;
    }

    /* Get current time */
    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Write header */
    fwprintf(f, L"=== FakeEsptool Device Dump ===\n");
    fwprintf(f, L"File: %ls\n", snap->device.filename[0] ? snap->device.filename : L"Untitled");
    fwprintf(f, L"Date: %04d-%02d-%02d %02d:%02d:%02d\n\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    /* Write header info */
    fwprintf(f, L"[Header]\n");
    fwprintf(f, L"Magic:      0x%08X (\"ESP\\0\")\n", DEVICE_MAGIC);
    fwprintf(f, L"Version:    %d\n", DEVICE_VERSION);

    /* Get chip name */
    WCHAR chipName[32] = {0};
    MultiByteToWideChar(CP_UTF8, 0, snap->device.chip.name, -1, chipName, 32);
    fwprintf(f, L"Chip Type:  %ls\n", chipName);

    /* Get xtal freq */
    const WCHAR *xtalStr = (snap->device.chip.xtal_freq == 0) ? L"40MHz" : L"26MHz";
    fwprintf(f, L"XTAL Freq:  %ls\n\n", xtalStr);

    /* Write MAC address */
    fwprintf(f, L"[MAC Address]\n");
    const BYTE *mac = snap->device.chip.mac;
    fwprintf(f, L"%02X:%02X:%02X:%02X:%02X:%02X\n\n",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Write Flash config */
    fwprintf(f, L"[Flash Config]\n");
    if (snap->flashSize >= 1024 * 1024)
        fwprintf(f, L"Size:       %luMB (%lu bytes)\n\n", snap->flashSize / (1024 * 1024), snap->flashSize);
    else
        fwprintf(f, L"Size:       %luKB (%lu bytes)\n\n", snap->flashSize / 1024, snap->flashSize);

    /* Write eFuse data */
    fwprintf(f, L"[eFuse] (%lu bytes)\n", snap->efuseSize);
    fwprintf(f, L"Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII\n");
    fwprintf(f, L"--------  -----------------------  -----------------------  ----------------\n");

    for (DWORD i = 0; i < snap->efuseSize; i += 16) {
        fwprintf(f, L"%08X  ", i);
        /* Hex bytes */
        for (DWORD j = 0; j < 16; j++) {
            if (i + j < snap->efuseSize)
                fwprintf(f, L"%02X ", snap->efuse[i + j]);
            else
                fwprintf(f, L"   ");
            if (j == 7) fwprintf(f, L" ");
        }
        fwprintf(f, L" ");
        /* ASCII */
        for (DWORD j = 0; j < 16 && (i + j) < snap->efuseSize; j++) {
            BYTE ch = snap->efuse[i + j];
            fwprintf(f, L"%c", (ch >= 32 && ch < 127) ? (WCHAR)ch : L'.');
        }
        fwprintf(f, L"\n");
    }
    fwprintf(f, L"\n");

    /* Write Flash data */
    fwprintf(f, L"[Flash Data] (%lu bytes)\n", snap->flashSize);
    fwprintf(f, L"Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII\n");
    fwprintf(f, L"--------  -----------------------  -----------------------  ----------------\n");

    for (DWORD i = 0; i < snap->flashSize; i += 16) {
        fwprintf(f, L"%08X  ", i);
        /* Hex bytes */
        for (DWORD j = 0; j < 16; j++) {
            if (i + j < snap->flashSize)
                fwprintf(f, L"%02X ", snap->flash[i + j]);
            else
                fwprintf(f, L"   ");
            if (j == 7) fwprintf(f, L" ");
        }
        fwprintf(f, L" ");
        /* ASCII */
        for (DWORD j = 0; j < 16 && (i + j) < snap->flashSize; j++) {
            BYTE ch = snap->flash[i + j];
            fwprintf(f, L"%c", (ch >= 32 && ch < 127) ? (WCHAR)ch : L'.');
        }
        fwprintf(f, L"\n");
    }

cleanup:
    if (f) fclose(f);

    /* Notify main window */
    PostMessage(snap->hWnd, WM_DUMP_COMPLETE, ok ? TRUE : FALSE, ok ? 0 : GetLastError());

    /* Free snapshot data */
    if (snap->efuse) HeapFree(GetProcessHeap(), 0, snap->efuse);
    if (snap->flash) HeapFree(GetProcessHeap(), 0, snap->flash);
    HeapFree(GetProcessHeap(), 0, snap);

    return ok ? 0 : 1;
}

/* Handle File > Dump Device As command */
void Main_OnDumpDeviceAs(HWND hMainWnd)
{
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};

    /* Generate default filename */
    const WCHAR *devName = Device_GetFilename(&g_device);
    if (devName[0]) {
        /* Extract name without path and extension */
        const WCHAR *name = wcsrchr(devName, L'\\');
        name = name ? name + 1 : devName;
        lstrcpyW(szFile, name);
        /* Remove .esp extension */
        WCHAR *ext = wcsrchr(szFile, L'.');
        if (ext) *ext = L'\0';
        lstrcatW(szFile, L"_dump.txt");
    } else {
        lstrcpyW(szFile, L"device_dump.txt");
    }

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"txt";

    if (!GetSaveFileNameW(&ofn))
        return;

    /* Create snapshot */
    DEVICE_SNAPSHOT *snap = (DEVICE_SNAPSHOT *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DEVICE_SNAPSHOT));
    if (!snap) {
        MessageBoxW(hMainWnd, L"Failed to allocate memory", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    /* Copy device info */
    snap->device = g_device;
    snap->hWnd = hMainWnd;
    lstrcpyW(snap->filename, szFile);

    /* Snapshot eFuse data */
    snap->efuseSize = g_device.chip.efuse_size;
    if (snap->efuseSize > 0 && g_device.chip.efuse) {
        snap->efuse = (BYTE *)HeapAlloc(GetProcessHeap(), 0, snap->efuseSize);
        if (!snap->efuse) {
            HeapFree(GetProcessHeap(), 0, snap);
            MessageBoxW(hMainWnd, L"Failed to allocate eFuse snapshot", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
            return;
        }
        memcpy(snap->efuse, g_device.chip.efuse, snap->efuseSize);
    }

    /* Snapshot Flash data */
    snap->flashSize = g_device.flash.size;
    if (snap->flashSize > 0 && g_device.flash.data) {
        snap->flash = (BYTE *)HeapAlloc(GetProcessHeap(), 0, snap->flashSize);
        if (!snap->flash) {
            if (snap->efuse) HeapFree(GetProcessHeap(), 0, snap->efuse);
            HeapFree(GetProcessHeap(), 0, snap);
            MessageBoxW(hMainWnd, L"Failed to allocate Flash snapshot", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
            return;
        }
        memcpy(snap->flash, g_device.flash.data, snap->flashSize);
    }

    /* Show busy cursor and disable window */
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    /* Start dump thread */
    HANDLE hThread = CreateThread(NULL, 0, DumpThreadProc, snap, 0, NULL);
    if (!hThread) {
        EnableWindow(hMainWnd, TRUE);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        if (snap->efuse) HeapFree(GetProcessHeap(), 0, snap->efuse);
        if (snap->flash) HeapFree(GetProcessHeap(), 0, snap->flash);
        HeapFree(GetProcessHeap(), 0, snap);
        MessageBoxW(hMainWnd, L"Failed to create dump thread", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(hThread); /* Thread will run independently */
}

/* Handle Log > Clear command */
void Main_OnLogClear(HWND hMainWnd)
{
    (void)hMainWnd;
    if (g_hEdit)
        SetWindowTextW(g_hEdit, L"");
}

/* Handle Log > Font command - show font selection dialog */
void Main_OnLogFont(HWND hMainWnd)
{
    CHOOSEFONTW cf = {0};
    LOGFONTW lf = g_logFont;

    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hMainWnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_FIXEDPITCHONLY | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
    cf.nFontType = SCREEN_FONTTYPE;

    if (ChooseFontW(&cf)) {
        g_logFont = lf;
        ApplyFontToEdit(g_hEdit, &g_logFont);
        Config_SetFont(&g_logFont);  /* Save to config */
    }
}

/* Handle Log > Save As command - save log to UTF-8 file */
void Main_OnLogSaveAs(HWND hMainWnd)
{
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};

    /* Generate default filename with timestamp */
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(szFile, L"FakeEsptool_%04d%02d%02d_%02d%02d%02d.log",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = LoadStr(IDS_LOG_SAVE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"log";

    if (!GetSaveFileNameW(&ofn))
        return;

    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_SAVE_ERROR), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    int textLen = GetWindowTextLengthW(g_hEdit);
    if (textLen > 0) {
        WCHAR *buf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (textLen + 1) * sizeof(WCHAR));
        if (buf) {
            GetWindowTextW(g_hEdit, buf, textLen + 1);

            /* Convert to UTF-8 (no BOM) */
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf, textLen + 1, NULL, 0, NULL, NULL);
            if (utf8Len > 0) {
                char *utf8Buf = (char *)HeapAlloc(GetProcessHeap(), 0, utf8Len);
                if (utf8Buf) {
                    WideCharToMultiByte(CP_UTF8, 0, buf, textLen + 1, utf8Buf, utf8Len, NULL, NULL);
                    DWORD written;
                    /* Exclude null terminator from output */
                    WriteFile(hFile, utf8Buf, utf8Len - 1, &written, NULL);
                    HeapFree(GetProcessHeap(), 0, utf8Buf);
                }
            }
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }

    CloseHandle(hFile);
}

/* Handle Exit command with confirmation if connected */
void Main_OnExit(HWND hWnd)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return;
    if (!PromptSaveIfNeeded(hWnd))
        return;
    DestroyWindow(hWnd);
}
