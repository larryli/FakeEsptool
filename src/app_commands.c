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

/* Encryption state and download mode for testing */
typedef enum {
    ENCRYPT_STATE_NONE = 0,
    ENCRYPT_STATE_DEV = 1,
    ENCRYPT_STATE_PROD = 2,
} ENCRYPT_STATE;

typedef enum {
    DOWNLOAD_MODE_NORMAL = 0,
    DOWNLOAD_MODE_SECURE = 1,
    DOWNLOAD_MODE_DISABLED = 2,
} DOWNLOAD_MODE;

static ENCRYPT_STATE g_encryptState = ENCRYPT_STATE_NONE;
static DOWNLOAD_MODE g_downloadMode = DOWNLOAD_MODE_NORMAL;

/*
 * PromptDisconnectIfNeeded - Check if serial is connected, prompt to disconnect
 */
BOOL PromptDisconnectIfNeeded(HWND hWnd)
{
    if (!Serial_IsOpen(&g_serial))
        return TRUE;

    int ret = MessageBoxW(hWnd,
                          LoadStr(IDS_MSG_CONFIRM_DISCONN),
                          LoadStr(IDS_MSG_DISCONN_CAP),
                          MB_YESNO | MB_ICONQUESTION);
    if (ret != IDYES)
        return FALSE;

    Serial_Close(&g_serial);
    UpdateTitle(hWnd);
    UpdateMenuState(hWnd);
    UpdateStatusBar();
    return TRUE;
}

/*
 * GenerateDefaultFilename - Generate default device filename
 *
 * Format: "[Chip]-[Flash Size]" (e.g. "ESP32-4MB")
 *
 * @buf:     Output buffer (MAX_PATH)
 */
static void GenerateDefaultFilename(WCHAR *buf)
{
    WCHAR chipName[32] = {0};
    MultiByteToWideChar(CP_UTF8, 0, g_device.chip.name, -1, chipName, 32);

    DWORD flashSize = g_device.flash.size;
    if (flashSize >= 1024 * 1024)
        wsprintfW(buf, L"%s-%luMB", chipName, flashSize / (1024 * 1024));
    else if (flashSize > 0)
        wsprintfW(buf, L"%s-%luKB", chipName, flashSize / 1024);
    else
        lstrcpyW(buf, chipName);
}

/*
 * PromptSaveIfNeeded - Check if device is modified, prompt to save
 */
BOOL PromptSaveIfNeeded(HWND hWnd)
{
    if (!Device_IsModified(&g_device))
        return TRUE;

    int ret = MessageBoxW(hWnd,
                          LoadStr(IDS_MSG_CONFIRM_SAVE),
                          LoadStr(IDS_MSG_SAVE_CAP),
                          MB_YESNOCANCEL | MB_ICONQUESTION);
    switch (ret) {
    case IDYES:
        {
            const WCHAR *filename = Device_GetFilename(&g_device);
            if (filename[0]) {
                if (Device_Save(&g_device, filename)) {
                    Config_SetLastDeviceFile(filename);
                } else {
                    MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
                }
            } else {
                OPENFILENAMEW ofn = {0};
                WCHAR szFile[MAX_PATH] = {0};
                GenerateDefaultFilename(szFile);
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFilter = LoadStr(IDS_DEVICE_FILTER);
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                ofn.lpstrDefExt = L"esp";
                if (!GetSaveFileNameW(&ofn))
                    return FALSE;
                if (Device_Save(&g_device, szFile)) {
                    Config_SetLastDeviceFile(szFile);
                } else {
                    MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
                }
            }
            UpdateTitle(hWnd);
        }
        return TRUE;
    case IDNO:
        return TRUE;
    default:
        return FALSE;
    }
}

/*
 * IsPortAvailable - Check if a specific port exists in the system
 */
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

/*
 * CanReconnect - Check if reconnect is available
 */
BOOL CanReconnect(void)
{
    if (Serial_IsOpen(&g_serial))
        return FALSE;
    if (!g_szPort[0])
        return FALSE;
    return IsPortAvailable(g_szPort);
}

/*
 * UpdateEncryptionMenu - Update encryption state menu check marks
 *
 * Auto-detects from eFuse when chip is available, falls back to manual toggle.
 *
 * @hMenu: Menu handle
 */
static void UpdateEncryptionMenu(HMENU hMenu)
{
    ENCRYPT_STATE state = g_encryptState;
    if (g_device.chip.name[0]) {
        BOOL encrypted = Chip_IsFlashEncryptionEnabled(&g_device.chip);
        BOOL prod = Chip_IsDownloadEncryptDisabled(&g_device.chip);
        if (encrypted && prod) state = ENCRYPT_STATE_PROD;
        else if (encrypted) state = ENCRYPT_STATE_DEV;
        else state = ENCRYPT_STATE_NONE;
    }
    CheckMenuItem(hMenu, IDM_ENCRYPT_NONE, 
        state == ENCRYPT_STATE_NONE ? (MF_CHECKED | MFT_RADIOCHECK) : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_ENCRYPT_DEV, 
        state == ENCRYPT_STATE_DEV ? (MF_CHECKED | MFT_RADIOCHECK) : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_ENCRYPT_PROD, 
        state == ENCRYPT_STATE_PROD ? (MF_CHECKED | MFT_RADIOCHECK) : MF_UNCHECKED);
}

/*
 * UpdateDownloadMenu - Update download mode menu check marks
 *
 * Auto-detects from eFuse when chip is available, falls back to manual toggle.
 *
 * @hMenu: Menu handle
 */
static void UpdateDownloadMenu(HMENU hMenu)
{
    DOWNLOAD_MODE mode = g_downloadMode;
    if (g_device.chip.name[0]) {
        BOOL dl_disabled = Chip_IsDownloadModeDisabled(&g_device.chip);
        BOOL secure = Chip_IsSecureDownloadEnabled(&g_device.chip);
        if (dl_disabled) mode = DOWNLOAD_MODE_DISABLED;
        else if (secure) mode = DOWNLOAD_MODE_SECURE;
        else mode = DOWNLOAD_MODE_NORMAL;
    }
    CheckMenuItem(hMenu, IDM_DOWNLOAD_NORMAL, 
        mode == DOWNLOAD_MODE_NORMAL ? (MF_CHECKED | MFT_RADIOCHECK) : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_DOWNLOAD_SECURE, 
        mode == DOWNLOAD_MODE_SECURE ? (MF_CHECKED | MFT_RADIOCHECK) : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_DOWNLOAD_DISABLED, 
        mode == DOWNLOAD_MODE_DISABLED ? (MF_CHECKED | MFT_RADIOCHECK) : MF_UNCHECKED);
}

/*
 * Main_CmdEncryptState - Handle encryption state menu command
 *
 * @hWnd: Main window handle
 * @state: New encryption state (0=none, 1=dev, 2=prod)
 */
void Main_CmdEncryptState(HWND hWnd, int state)
{
    g_encryptState = (ENCRYPT_STATE)state;
    UpdateEncryptionMenu(GetMenu(hWnd));
    UpdateStatusBar();
}

/*
 * Main_CmdDownloadMode - Handle download mode menu command
 *
 * @hWnd: Main window handle
 * @mode: New download mode (0=normal, 1=secure, 2=disabled)
 */
void Main_CmdDownloadMode(HWND hWnd, int mode)
{
    g_downloadMode = (DOWNLOAD_MODE)mode;
    UpdateDownloadMenu(GetMenu(hWnd));
    UpdateStatusBar();
}

/*
 * GetEncryptStateStrId - Get string ID for current encryption state
 *
 * Uses eFuse state when chip is available, falls back to manual toggle.
 */
static UINT GetEncryptStateStrId(void)
{
    if (g_device.chip.name[0]) {
        BOOL encrypted = Chip_IsFlashEncryptionEnabled(&g_device.chip);
        BOOL prod = Chip_IsDownloadEncryptDisabled(&g_device.chip);
        if (encrypted && prod) return IDS_ENCRYPT_PROD;
        if (encrypted) return IDS_ENCRYPT_DEV;
        return IDS_ENCRYPT_NONE;
    }
    switch (g_encryptState) {
    case ENCRYPT_STATE_DEV:  return IDS_ENCRYPT_DEV;
    case ENCRYPT_STATE_PROD: return IDS_ENCRYPT_PROD;
    default:                 return IDS_ENCRYPT_NONE;
    }
}

/*
 * GetDownloadModeStrId - Get string ID for current download mode
 *
 * Uses eFuse state when chip is available, falls back to manual toggle.
 */
static UINT GetDownloadModeStrId(void)
{
    if (g_device.chip.name[0]) {
        BOOL dl_disabled = Chip_IsDownloadModeDisabled(&g_device.chip);
        BOOL secure = Chip_IsSecureDownloadEnabled(&g_device.chip);
        if (dl_disabled) return IDS_DOWNLOAD_DISABLED;
        if (secure) return IDS_DOWNLOAD_SECURE;
        return IDS_DOWNLOAD_NORMAL;
    }
    switch (g_downloadMode) {
    case DOWNLOAD_MODE_SECURE:   return IDS_DOWNLOAD_SECURE;
    case DOWNLOAD_MODE_DISABLED: return IDS_DOWNLOAD_DISABLED;
    default:                     return IDS_DOWNLOAD_NORMAL;
    }
}

/*
 * UpdateMenuState - Update menu and toolbar button states
 *
 * Enables/disables menu items and toolbar buttons based on
 * current connection status and device state.
 *
 * @hWnd: Main window handle
 */
void UpdateMenuState(HWND hWnd)
{
    HMENU hMenu = GetMenu(hWnd);
    BOOL connected = Serial_IsOpen(&g_serial);
    BOOL canReconnect = CanReconnect();
    BOOL canKeyMgmt = g_device.chip.type != CHIP_ESP8266;

    EnableMenuItem(hMenu, IDM_CONNECT, connected ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, IDM_DISCONNECT, connected ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_RECONNECT, canReconnect ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_KEY_MGMT, canKeyMgmt ? MF_ENABLED : MF_GRAYED);

    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_CONNECT, !connected);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_DISCONNECT, connected);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_RECONNECT, canReconnect);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_KEY_MGMT, canKeyMgmt);

    /* Update encryption state and download mode menu check marks */
    UpdateEncryptionMenu(hMenu);
    UpdateDownloadMenu(hMenu);
}

/*
 * UpdateTitle - Update window title bar
 *
 * Format: "FakeEsptool - [Chip] - [File][*] - [Port]"
 * Shows chip type, filename, modified indicator, and port name.
 *
 * @hWnd: Main window handle
 */
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
        lstrcatW(p, LoadStr(IDS_TITLE_UNTITLED));
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

/*
 * UpdateStatusBar - Update status bar display
 *
 * Updates all 5 status bar parts:
 * 1. Chip type (e.g. "ESP32")
 * 2. Flash size (e.g. "4MB")
 * 3. MAC address (e.g. "AA:BB:CC:DD:EE:01")
 * 4. Port name (e.g. "COM10") or "Disconnected"
 * 5. Port config (e.g. "115200,8N1")
 */
void UpdateStatusBar(void)
{
    if (!g_hStatusbar)
        return;

    int parts[7];
    RECT rc;
    GetClientRect(GetParent(g_hStatusbar), &rc);
    parts[0] = STATUS_PART1_WIDTH;
    parts[1] = parts[0] + STATUS_PART2_WIDTH;
    parts[2] = parts[1] + STATUS_PART3_WIDTH;
    parts[3] = parts[2] + STATUS_PART4_WIDTH;
    parts[4] = parts[3] + STATUS_PART5_WIDTH;
    parts[5] = parts[4] + STATUS_PART6_WIDTH;
    parts[6] = -1;
    SendMessageW(g_hStatusbar, SB_SETPARTS, 7, (LPARAM)parts);

    /* Part 1: Chip type */
    if (g_device.chip.name[0]) {
        WCHAR chipName[32];
        MultiByteToWideChar(CP_UTF8, 0, g_device.chip.name, -1, chipName, 32);
        SendMessageW(g_hStatusbar, SB_SETTEXT, 0, (LPARAM)chipName);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 0, (LPARAM)LoadStr(IDS_STATUS_NO_DEVICE));
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

    /* Part 4: Encryption status */
    if (g_device.chip.name[0]) {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 3, (LPARAM)LoadStr(GetEncryptStateStrId()));
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 3, (LPARAM)L"");
    }

    /* Part 5: Download mode status */
    if (g_device.chip.name[0]) {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 4, (LPARAM)LoadStr(GetDownloadModeStrId()));
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 4, (LPARAM)L"");
    }

    /* Part 6 & 7: Serial port and config */
    if (Serial_IsOpen(&g_serial)) {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 5, (LPARAM)g_szPort);

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
        SendMessageW(g_hStatusbar, SB_SETTEXT, 6, (LPARAM)configBuf);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 5, (LPARAM)LoadStr(IDS_DISCONNECTED));
        SendMessageW(g_hStatusbar, SB_SETTEXT, 6, (LPARAM)L"");
    }
}

/*
 * ApplyFontToEdit - Apply font to RichEdit control
 *
 * @hEdit: Handle to RichEdit control
 * @plf:   Pointer to LOGFONTW structure with font settings
 */
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

/*
 * InitDefaultFont - Initialize default font settings
 *
 * Tries to load font from config file. If not available,
 * uses Consolas 10pt as default monospace font.
 */
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

/*
 * Main_CmdNewDevice - Handle New Device command
 *
 * Creates a new default device (ESP32, 40MHz, 4MB).
 * Prompts to disconnect and save if needed.
 *
 * @hWnd: Main window handle
 */
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
        UpdateMenuState(hWnd);
        UpdateStatusBar();
        UpdateTitle(hWnd);
        SetWindowTextW(g_hEdit, L"");
    } else {
        MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_CREATE_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
    }
}

/*
 * Main_CmdOpenDevice - Handle Open Device command
 *
 * Shows file open dialog and loads selected .esp file.
 * Prompts to disconnect and save if needed.
 *
 * @hWnd: Main window handle
 */
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
            UpdateMenuState(hWnd);
            UpdateStatusBar();
            UpdateTitle(hWnd);
            SetWindowTextW(g_hEdit, L"");
        } else {
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_LOAD_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        }
    }
}

/*
 * Main_OpenDeviceFile - Open device file by path
 *
 * Used by command line and drag-drop to open a device file.
 * Prompts to disconnect and save if needed.
 *
 * @hWnd:     Main window handle
 * @filePath: Path to .esp file
 *
 * Returns TRUE on success.
 */
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
        UpdateMenuState(hWnd);
        UpdateStatusBar();
        UpdateTitle(hWnd);
        SetWindowTextW(g_hEdit, L"");
        return TRUE;
    } else {
        MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_LOAD_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return FALSE;
    }
}

/*
 * Main_CmdSaveDevice - Handle Save Device command
 *
 * Saves device to current file. If no file, does Save As.
 *
 * @hWnd: Main window handle
 */
void Main_CmdSaveDevice(HWND hWnd)
{
    const WCHAR *filename = Device_GetFilename(&g_device);
    if (filename[0]) {
        if (Device_Save(&g_device, filename)) {
            Config_SetLastDeviceFile(filename);
            UpdateTitle(hWnd);
        } else {
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        }
    } else {
        /* No filename, do Save As */
        Main_CmdSaveDeviceAs(hWnd);
    }
}

/*
 * Main_CmdSaveDeviceAs - Handle Save Device As command
 *
 * Shows file save dialog and saves device to selected path.
 *
 * @hWnd: Main window handle
 */
void Main_CmdSaveDeviceAs(HWND hWnd)
{
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    GenerateDefaultFilename(szFile);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = LoadStr(IDS_DEVICE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"esp";
    if (GetSaveFileNameW(&ofn)) {
        if (Device_Save(&g_device, szFile)) {
            Config_SetLastDeviceFile(szFile);
            UpdateTitle(hWnd);
        } else {
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        }
    }
}

/*
 * Main_CmdDeviceProps - Handle Device Properties command
 *
 * Shows device properties dialog. Prompts to disconnect and save if needed.
 *
 * @hWnd: Main window handle
 */
void Main_CmdDeviceProps(HWND hWnd)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return;
    if (!PromptSaveIfNeeded(hWnd))
        return;
    if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_DEVICE_PROPS), hWnd, DevicePropsDlgProc) == IDOK) {
        UpdateMenuState(hWnd);
        UpdateStatusBar();
        UpdateTitle(hWnd);
    }
}

/*
 * Main_CmdKeyMgmt - Handle Key Management command
 *
 * Shows key management dialog for flash encryption keys.
 * If serial is connected, prompts to disconnect first.
 *
 * @hWnd: Main window handle
 */
void Main_CmdKeyMgmt(HWND hWnd)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return;
    DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_KEY_MGMT), hWnd, KeyMgmtDlgProc);
}

/*
 * Main_OnConnect - Handle Connect command
 *
 * Shows port selection dialog and opens selected serial port.
 * Registers esptool protocol callbacks and clears log.
 *
 * @hMainWnd: Main window handle
 */
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

/*
 * Main_OnReconnect - Handle Reconnect command
 *
 * Connects directly to last used port without showing dialog.
 * Port must exist and not be already connected.
 *
 * @hMainWnd: Main window handle
 */
void Main_OnReconnect(HWND hMainWnd)
{
    TRACE_FW(TAG, "Main_OnReconnect called");

    if (Serial_IsOpen(&g_serial)) {
        TRACE_FW(TAG, "Port already open");
        return;
    }

    if (!g_szPort[0]) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_NO_LAST_PORT), LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
        return;
    }

    if (!IsPortAvailable(g_szPort)) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_PORT_NOT_AVAIL), LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
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

/*
 * Main_OnDisconnect - Handle Disconnect command
 *
 * Closes serial port and updates UI state.
 *
 * @hMainWnd: Main window handle
 */
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

/*
 * Main_OnFlashImport - Handle Flash Import command
 *
 * Imports flash data from .bin file. File size must match current flash size.
 * Prompts to disconnect and save if needed.
 *
 * @hMainWnd: Main window handle
 */
void Main_OnFlashImport(HWND hMainWnd)
{
    if (!PromptDisconnectIfNeeded(hMainWnd))
        return;
    if (!PromptSaveIfNeeded(hMainWnd))
        return;

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
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_OPEN_FILE), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize != g_device.flash.size) {
        CloseHandle(hFile);
        WCHAR msg[128];
        wsprintfW(msg, LoadStr(IDS_MSG_FLASH_MISMATCH), fileSize, g_device.flash.size);
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
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_READ_FILE), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    Device_SetModified(&g_device, TRUE);
    UpdateTitle(hMainWnd);
}

/*
 * Main_OnFlashExport - Handle Flash Export command
 *
 * Exports flash data to .bin file using snapshot to avoid data corruption.
 *
 * @hMainWnd: Main window handle
 */
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
            MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC_SNAP), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_CREATE_FILE), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_WRITE_FILE), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    HeapFree(GetProcessHeap(), 0, flashSnapshot);
}

/*
 * DEVICE_SNAPSHOT - Device data snapshot for Dump Device As
 *
 * Contains copies of device data for background thread processing.
 * Thread frees this structure when done.
 */
typedef struct {
    DEVICE_CTX device;      /* Device header info */
    BYTE *efuse;            /* eFuse data snapshot */
    DWORD efuseSize;        /* eFuse size */
    BYTE *flash;            /* Flash data snapshot */
    DWORD flashSize;        /* Flash size */
    WCHAR filename[MAX_PATH]; /* Output filename */
    HWND hWnd;              /* Owner window */
} DEVICE_SNAPSHOT;

/*
 * DumpThreadProc - Background thread for device dump
 *
 * Writes device data to text file in hex dump format.
 * Notifies main window via WM_DUMP_COMPLETE when done.
 *
 * @lpParam: Pointer to DEVICE_SNAPSHOT structure (freed by thread)
 *
 * Returns 0 on success, 1 on failure.
 */
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

/*
 * Main_OnDumpDeviceAs - Handle Dump Device As command
 *
 * Exports device data to text file with hex dump format.
 * Uses background thread to avoid blocking UI.
 *
 * @hMainWnd: Main window handle
 */
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
    ofn.lpstrFilter = LoadStr(IDS_TXT_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"txt";

    if (!GetSaveFileNameW(&ofn))
        return;

    /* Create snapshot */
    DEVICE_SNAPSHOT *snap = (DEVICE_SNAPSHOT *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DEVICE_SNAPSHOT));
    if (!snap) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
            MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC_EFUSE), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
            MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC_FLASH), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_DUMP_THREAD), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(hThread); /* Thread will run independently */
}

/*
 * Main_OnLogClear - Handle Log Clear command
 *
 * Clears all content from the log display.
 *
 * @hMainWnd: Main window handle (unused)
 */
void Main_OnLogClear(HWND hMainWnd)
{
    (void)hMainWnd;
    if (g_hEdit)
        SetWindowTextW(g_hEdit, L"");
}

/*
 * Main_OnLogFont - Handle Log Font command
 *
 * Shows font selection dialog. Only monospace fonts are listed.
 * Saves selected font to config file.
 *
 * @hMainWnd: Main window handle
 */
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

/*
 * Main_OnLogSaveAs - Handle Log Save As command
 *
 * Saves log content to UTF-8 text file.
 * Default filename includes timestamp.
 *
 * @hMainWnd: Main window handle
 */
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

/*
 * Main_OnExit - Handle Exit command
 *
 * Prompts to disconnect and save if needed, then destroys window.
 *
 * @hWnd: Main window handle
 */
void Main_OnExit(HWND hWnd)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return;
    if (!PromptSaveIfNeeded(hWnd))
        return;
    DestroyWindow(hWnd);
}
