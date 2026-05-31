/*
 * main.c - FakeEsptool Application
 *
 * Main entry point, GUI implementation with toolbar, status bar,
 * and RichEdit log display. Handles menu commands and serial port events.
 */

#include <windows.h>
#include "main.h"
#include "serial.h"
#include "esptool_proto.h"
#include "esptool/device.h"
#include "resource.h"
#include "utils/config.h"
#include "utils/lang.h"
#include "utils/trace.h"
#include <richedit.h>
#include <commdlg.h>
#include <dbt.h>
#include <devguid.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winver.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Version.lib")

#pragma comment(lib, "comctl32.lib")

#if ENABLE_TRACE
static const char *TAG = "GUI";
#endif

/* Default font settings */
#define DEFAULT_FONT_NAME L"Consolas"
#define DEFAULT_FONT_SIZE 10

/* Status bar part widths */
#define STATUS_PART1_WIDTH  100  /* Chip model */
#define STATUS_PART2_WIDTH  80   /* Flash size */
#define STATUS_PART3_WIDTH  150  /* MAC address */
#define STATUS_PART4_WIDTH  120  /* Port name */
#define STATUS_PART5_WIDTH  140  /* Port config */

/* Message size limit */
#define MAX_MSG_SIZE        65536

/* Global state */
static SERIAL_CTX g_serial = { .hPort = NULL, .hThread = NULL, .hStartEvent = NULL, .hNotify = NULL, .bRunning = FALSE };
static DEVICE_CTX g_device = {0};
static HWND g_hWnd = NULL;           /* Main window handle */
static HWND g_hToolbar = NULL;
static HWND g_hStatusbar = NULL;
static HWND g_hEdit = NULL;
static HMODULE g_hRichEdit = NULL;   /* RichEdit DLL handle */
static HDEVNOTIFY g_hDevNotify = NULL;  /* Device notification handle */
static WCHAR g_szPort[32] = {0};
static WCHAR g_szSelectedPort[32] = {0};
static LOGFONTW g_logFont = {0};  /* Current font */

/* Forward declarations */
static void UpdateMenuState(HWND hWnd);
static void UpdateTitle(HWND hWnd);
static void UpdateStatusBar(void);

/* Callback when device data is modified by protocol */
static void OnDeviceModified(void)
{
    Device_SetModified(&g_device, TRUE);
    if (g_hWnd)
        UpdateTitle(g_hWnd);
}

/* Check if serial is connected, prompt to disconnect if needed */
static BOOL PromptDisconnectIfNeeded(HWND hWnd)
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

/* Check if device is modified, prompt to save if needed */
static BOOL PromptSaveIfNeeded(HWND hWnd)
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

/* Check if a specific port exists in the system */
static BOOL IsPortAvailable(const WCHAR *portName)
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

/* Check if reconnect is available (last port exists and not connected) */
static BOOL CanReconnect(void)
{
    if (Serial_IsOpen(&g_serial))
        return FALSE;
    if (!g_szPort[0])
        return FALSE;
    return IsPortAvailable(g_szPort);
}

/* Update menu and toolbar button states based on connection status */
static void UpdateMenuState(HWND hWnd)
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
static void UpdateTitle(HWND hWnd)
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
static void UpdateStatusBar(void)
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

/* New Device dialog procedure */
static INT_PTR CALLBACK NewDeviceDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static BYTE mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    static CHIP_TYPE selectedChip = CHIP_ESP8266;
    static DWORD selectedFlash = 4 * 1024 * 1024;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        {
            HWND hChip = GetDlgItem(hDlg, IDC_CHIP_COMBO);
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP8266");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-S2");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-S3");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C2");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C3");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C6");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C61");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-H2");
            SendMessageW(hChip, CB_SETCURSEL, 0, 0);

            HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"256KB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"512KB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"1MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"2MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"4MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"8MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"16MB");
            SendMessageW(hFlash, CB_SETCURSEL, 4, 0);

            HWND hMode = GetDlgItem(hDlg, IDC_FLASH_MODE_COMBO);
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"QIO");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"QOUT");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"DIO");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"DOUT");
            SendMessageW(hMode, CB_SETCURSEL, 2, 0);

            HWND hFreq = GetDlgItem(hDlg, IDC_FLASH_FREQ_COMBO);
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"40MHz");
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"26MHz");
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"20MHz");
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"80MHz");
            SendMessageW(hFreq, CB_SETCURSEL, 0, 0);

            CheckDlgButton(hDlg, IDC_INIT_BLANK, BST_CHECKED);
            EnableWindow(GetDlgItem(hDlg, IDC_INIT_FILE_PATH), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_FILE), FALSE);

            WCHAR macStr[32];
            wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            SetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr);

            srand((unsigned)time(NULL));
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_RANDOM_MAC:
            {
                mac[0] = 0xAA;
                mac[1] = 0xBB;
                mac[2] = (BYTE)(rand() & 0xFF);
                mac[3] = (BYTE)(rand() & 0xFF);
                mac[4] = (BYTE)(rand() & 0xFF);
                mac[5] = (BYTE)(rand() & 0xFF);
                WCHAR macStr[32];
                wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                SetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr);
            }
            return TRUE;

        case IDC_INIT_FILE:
            EnableWindow(GetDlgItem(hDlg, IDC_INIT_FILE_PATH), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_FILE), TRUE);
            return TRUE;

        case IDC_INIT_BLANK:
            EnableWindow(GetDlgItem(hDlg, IDC_INIT_FILE_PATH), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_FILE), FALSE);
            return TRUE;

        case IDC_BROWSE_FILE:
            {
                OPENFILENAMEW ofn = {0};
                WCHAR szFile[MAX_PATH] = {0};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hDlg;
                ofn.lpstrFilter = L"Binary Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    SetDlgItemTextW(hDlg, IDC_INIT_FILE_PATH, szFile);
                }
            }
            return TRUE;

        case IDOK:
            {
                HWND hChip = GetDlgItem(hDlg, IDC_CHIP_COMBO);
                int chipSel = (int)SendMessageW(hChip, CB_GETCURSEL, 0, 0);
                selectedChip = (CHIP_TYPE)chipSel;

                HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
                int flashSel = (int)SendMessageW(hFlash, CB_GETCURSEL, 0, 0);
                DWORD flashSizes[] = {256*1024, 512*1024, 1024*1024, 2*1024*1024, 
                                      4*1024*1024, 8*1024*1024, 16*1024*1024};
                selectedFlash = flashSizes[flashSel];

                WCHAR macStr[32];
                GetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr, 32);
                swscanf(macStr, L"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

                Device_Close(&g_device);
                if (Device_Init(&g_device, selectedChip, selectedFlash, mac)) {
                    EndDialog(hDlg, IDOK);
                } else {
                    MessageBoxW(hDlg, L"Failed to create device", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Device Properties dialog procedure */
static INT_PTR CALLBACK DevicePropsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static BYTE mac[6];
    static CHIP_TYPE selectedChip;
    static DWORD selectedFlash;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        {
            /* Save current values */
            selectedChip = g_device.chip.type;
            selectedFlash = g_device.flash.size;
            memcpy(mac, g_device.chip.mac, 6);

            HWND hChip = GetDlgItem(hDlg, IDC_CHIP_COMBO);
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP8266");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-S2");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-S3");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C2");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C3");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C6");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C61");
            SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-H2");
            SendMessageW(hChip, CB_SETCURSEL, (WPARAM)selectedChip, 0);

            HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"256KB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"512KB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"1MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"2MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"4MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"8MB");
            SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)L"16MB");
            /* Select current flash size */
            DWORD flashSizes[] = {256*1024, 512*1024, 1024*1024, 2*1024*1024,
                                  4*1024*1024, 8*1024*1024, 16*1024*1024};
            int flashIdx = 4;
            for (int i = 0; i < 7; i++) {
                if (flashSizes[i] == selectedFlash) {
                    flashIdx = i;
                    break;
                }
            }
            SendMessageW(hFlash, CB_SETCURSEL, flashIdx, 0);

            HWND hMode = GetDlgItem(hDlg, IDC_FLASH_MODE_COMBO);
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"QIO");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"QOUT");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"DIO");
            SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)L"DOUT");
            SendMessageW(hMode, CB_SETCURSEL, g_device.chip.flash_mode, 0);

            HWND hFreq = GetDlgItem(hDlg, IDC_FLASH_FREQ_COMBO);
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"40MHz");
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"26MHz");
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"20MHz");
            SendMessageW(hFreq, CB_ADDSTRING, 0, (LPARAM)L"80MHz");
            SendMessageW(hFreq, CB_SETCURSEL, g_device.chip.flash_freq, 0);

            WCHAR macStr[32];
            wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            SetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr);

            srand((unsigned)time(NULL));
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_RANDOM_MAC:
            {
                mac[0] = 0xAA;
                mac[1] = 0xBB;
                mac[2] = (BYTE)(rand() & 0xFF);
                mac[3] = (BYTE)(rand() & 0xFF);
                mac[4] = (BYTE)(rand() & 0xFF);
                mac[5] = (BYTE)(rand() & 0xFF);
                WCHAR macStr[32];
                wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                SetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr);
            }
            return TRUE;

        case IDOK:
            {
                HWND hChip = GetDlgItem(hDlg, IDC_CHIP_COMBO);
                int chipSel = (int)SendMessageW(hChip, CB_GETCURSEL, 0, 0);
                selectedChip = (CHIP_TYPE)chipSel;

                HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
                int flashSel = (int)SendMessageW(hFlash, CB_GETCURSEL, 0, 0);
                DWORD flashSizes[] = {256*1024, 512*1024, 1024*1024, 2*1024*1024,
                                      4*1024*1024, 8*1024*1024, 16*1024*1024};
                selectedFlash = flashSizes[flashSel];

                WCHAR macStr[32];
                GetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr, 32);
                swscanf(macStr, L"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

                HWND hMode = GetDlgItem(hDlg, IDC_FLASH_MODE_COMBO);
                BYTE flashMode = (BYTE)SendMessageW(hMode, CB_GETCURSEL, 0, 0);

                HWND hFreq = GetDlgItem(hDlg, IDC_FLASH_FREQ_COMBO);
                BYTE flashFreq = (BYTE)SendMessageW(hFreq, CB_GETCURSEL, 0, 0);

                /* Check if anything changed */
                BOOL changed = (selectedChip != g_device.chip.type) ||
                               (selectedFlash != g_device.flash.size) ||
                               (memcmp(mac, g_device.chip.mac, 6) != 0) ||
                               (flashMode != g_device.chip.flash_mode) ||
                               (flashFreq != g_device.chip.flash_freq);

                if (changed) {
                    /* Reinitialize device with new settings */
                    FLASH_CTX oldFlash = g_device.flash;
                    g_device.flash.data = NULL;
                    g_device.flash.allocated = FALSE;

                    Device_Close(&g_device);
                    if (Device_Init(&g_device, selectedChip, selectedFlash, mac)) {
                        g_device.chip.flash_mode = flashMode;
                        g_device.chip.flash_freq = flashFreq;

                        /* Copy old flash data if same size or larger */
                        if (oldFlash.data && oldFlash.size <= selectedFlash) {
                            memcpy(g_device.flash.data, oldFlash.data, oldFlash.size);
                        }
                        Flash_Close(&oldFlash);

                        Device_SetModified(&g_device, TRUE);
                        EndDialog(hDlg, IDOK);
                    } else {
                        Flash_Close(&oldFlash);
                        MessageBoxW(hDlg, L"Failed to update device", L"Error", MB_OK | MB_ICONERROR);
                    }
                } else {
                    EndDialog(hDlg, IDOK);
                }
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Port selection dialog procedure */
static INT_PTR CALLBACK PortSelectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        {
            HWND hCombo = GetDlgItem(hDlg, IDC_PORT_COMBO);
            if (!Serial_EnumPorts(hCombo)) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }

            /* Try to select last connected port */
            WCHAR lastPort[32] = {0};
            if (Config_GetLastPort(lastPort, 32)) {
                int count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
                for (int i = 0; i < count; i++) {
                    int portIdx = (int)SendMessageW(hCombo, CB_GETITEMDATA, i, 0);
                    WCHAR portName[32] = {0};
                    if (Serial_GetPortName(portIdx, portName, 32)) {
                        if (lstrcmpiW(portName, lastPort) == 0) {
                            SendMessageW(hCombo, CB_SETCURSEL, i, 0);
                            break;
                        }
                    }
                }
            }

            SetFocus(hCombo);
        }
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            {
                HWND hCombo = GetDlgItem(hDlg, IDC_PORT_COMBO);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel < 0) {
                    MessageBoxW(hDlg, LoadStr(IDS_MSG_SELECT_PORT), LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                /* Get port index from item data */
                int portIdx = (int)SendMessageW(hCombo, CB_GETITEMDATA, sel, 0);
                if (!Serial_GetPortName(portIdx, g_szSelectedPort, 32)) {
                    MessageBoxW(hDlg, LoadStr(IDS_MSG_INVALID_PORT), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Show port selection dialog, returns TRUE if user selected a port */
static BOOL ShowPortSelectDialog(HWND hWnd)
{
    INT_PTR ret = DialogBoxW(GetModuleHandle(NULL),
                             MAKEINTRESOURCEW(IDD_PORT_SELECT), hWnd, PortSelectDlgProc);
    if (ret == IDOK) {
        lstrcpyW(g_szPort, g_szSelectedPort);
        return TRUE;
    }
    return FALSE;
}

/* Color definitions for log display */
#define COLOR_TIMESTAMP RGB(128, 128, 128)  /* Gray */
#define COLOR_RX        RGB(0, 0, 200)      /* Blue */
#define COLOR_TX        RGB(0, 128, 0)      /* Green */
#define COLOR_SIGNAL    RGB(128, 0, 128)    /* Purple */
#define COLOR_CONFIG    RGB(0, 128, 128)    /* Teal */
#define COLOR_CUSTOM    RGB(200, 100, 0)    /* Orange */
#define COLOR_DATA      RGB(0, 0, 0)        /* Black */
#define COLOR_BG        RGB(240, 240, 240)  /* Light gray */

/* Helper: Set text color for selection */
static void SetEditColor(HWND hEdit, COLORREF color)
{
    CHARFORMAT2W cf = {0};
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

/* Helper: Append colored text to RichEdit */
static void AppendColoredText(HWND hEdit, const WCHAR *text, int len, COLORREF color)
{
    int textLen = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, textLen, textLen);
    SetEditColor(hEdit, color);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

/* Format and append data to log display with colors */
static void Main_AppendLog(HWND hMainWnd, const BYTE *data, DWORD len, int dir)
{
    (void)hMainWnd;
    if (!g_hEdit || len == 0)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Build timestamp: "YYYY-MM-DD HH:MM:SS.mmm " */
    WCHAR timestamp[32];
    int tsLen = wsprintfW(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                          st.wYear, st.wMonth, st.wDay,
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    /* Build direction: "[RX] " or "[TX] " */
    WCHAR direction[8];
    int dirLen = wsprintfW(direction, L"[%s] ", (dir == DIR_RX) ? L"RX" : L"TX");
    COLORREF dirColor = (dir == DIR_RX) ? COLOR_RX : COLOR_TX;

    /* Calculate max line size for HEX data */
    DWORD numLines = (len + 15) / 16;
    DWORD maxLineSize = (tsLen + dirLen + 50) * (numLines + 1) + (len * 4) + 64;
    WCHAR *hexLine = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, maxLineSize * sizeof(WCHAR));
    if (!hexLine)
        return;

    int pos = 0;
    int prefixLen = tsLen + dirLen;

    /* Format HEX data with grouping and line wrapping */
    for (DWORD i = 0; i < len; i++) {
        if (i > 0 && i % 16 == 0) {
            /* New line, align with prefix */
            hexLine[pos++] = L'\r';
            hexLine[pos++] = L'\n';
            for (int j = 0; j < prefixLen; j++)
                hexLine[pos++] = L' ';
        } else if (i > 0 && i % 8 == 0) {
            /* Extra space every 8 bytes */
            hexLine[pos++] = L' ';
        }
        pos += wsprintfW(hexLine + pos, L"%02X ", data[i]);
    }
    hexLine[pos++] = L'\r';
    hexLine[pos++] = L'\n';
    hexLine[pos] = L'\0';

    /* Append with colors: timestamp (gray) + direction (color) + data (black) */
    AppendColoredText(g_hEdit, timestamp, tsLen, COLOR_TIMESTAMP);
    AppendColoredText(g_hEdit, direction, dirLen, dirColor);
    AppendColoredText(g_hEdit, hexLine, pos, COLOR_DATA);

    /* Scroll to end */
    int textLen = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);

    HeapFree(GetProcessHeap(), 0, hexLine);
}

/* Format and append custom text to log display */
static void Main_AppendCustomLog(HWND hMainWnd, const WCHAR *tag, const WCHAR *text)
{
    (void)hMainWnd;
    if (!g_hEdit || !tag || !text)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Build timestamp: "YYYY-MM-DD HH:MM:SS.mmm " */
    WCHAR timestamp[32];
    int tsLen = wsprintfW(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                          st.wYear, st.wMonth, st.wDay,
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    /* Build tag: "[tag] " */
    WCHAR tagStr[64];
    int tagLen = wsprintfW(tagStr, L"[%s] ", tag);

    /* Append with colors: timestamp (gray) + tag (orange) + text (black) */
    AppendColoredText(g_hEdit, timestamp, tsLen, COLOR_TIMESTAMP);
    AppendColoredText(g_hEdit, tagStr, tagLen, COLOR_CUSTOM);
    AppendColoredText(g_hEdit, text, lstrlenW(text), COLOR_DATA);

    /* Append newline */
    AppendColoredText(g_hEdit, L"\r\n", 2, COLOR_DATA);

    /* Scroll to end */
    int textLen = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
}

/* Format and append signal/config log with distinct colors */
static void Main_AppendSignalLog(const WCHAR *tag, const WCHAR *text, COLORREF tagColor)
{
    if (!g_hEdit || !tag || !text)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Build timestamp: "YYYY-MM-DD HH:MM:SS.mmm " */
    WCHAR timestamp[32];
    int tsLen = wsprintfW(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                          st.wYear, st.wMonth, st.wDay,
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    /* Build tag: "[tag] " */
    WCHAR tagStr[64];
    int tagLen = wsprintfW(tagStr, L"[%s] ", tag);

    /* Append with colors: timestamp (gray) + tag (signal/config color) + text (gray) */
    AppendColoredText(g_hEdit, timestamp, tsLen, COLOR_TIMESTAMP);
    AppendColoredText(g_hEdit, tagStr, tagLen, tagColor);
    AppendColoredText(g_hEdit, text, lstrlenW(text), COLOR_TIMESTAMP);

    /* Append newline */
    AppendColoredText(g_hEdit, L"\r\n", 2, COLOR_DATA);

    /* Scroll to end */
    int textLen = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
}

/* Handle Connect command */
static void Main_OnConnect(HWND hMainWnd)
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

    TRACE_FW(TAG, "Selected port: %s", g_szPort);
    TRACE_FW(TAG, "Calling Serial_Open...");

    if (!Serial_Open(&g_serial, g_szPort, hMainWnd)) {
        TRACE_FW(TAG, "ERROR: Serial_Open failed");
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_PORT_ERROR), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    /* Register esptool protocol callbacks */
    Serial_SetReceiveCallback(&g_serial, (SERIAL_RX_CB)EsptoolProto_ProcessData);
    Serial_SetSignalCallback(&g_serial, (SERIAL_SIGNAL_CB)EsptoolProto_OnSignal);

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
static void Main_OnReconnect(HWND hMainWnd)
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
    Serial_SetReceiveCallback(&g_serial, (SERIAL_RX_CB)EsptoolProto_ProcessData);
    Serial_SetSignalCallback(&g_serial, (SERIAL_SIGNAL_CB)EsptoolProto_OnSignal);

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
static void Main_OnDisconnect(HWND hMainWnd)
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
static void Main_OnFlashImport(HWND hMainWnd)
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

    DWORD bytesRead;
    ReadFile(hFile, g_device.flash.data, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    Device_SetModified(&g_device, TRUE);
    UpdateTitle(hMainWnd);
    Serial_PostLog(hMainWnd, L"FLASH", L"Flash imported from file");
}

/* Handle Flash > Export command */
static void Main_OnFlashExport(HWND hMainWnd)
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

    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hMainWnd, L"Failed to create file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD bytesWritten;
    WriteFile(hFile, g_device.flash.data, g_device.flash.size, &bytesWritten, NULL);
    CloseHandle(hFile);

    Serial_PostLog(hMainWnd, L"FLASH", L"Flash exported to file");
}

/* Handle Log > Clear command */
static void Main_OnLogClear(HWND hMainWnd)
{
    (void)hMainWnd;
    if (g_hEdit)
        SetWindowTextW(g_hEdit, L"");
}

/* Apply font to RichEdit control */
static void ApplyFontToEdit(HWND hEdit, LOGFONTW *plf)
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
static void InitDefaultFont(void)
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

/* Handle Log > Font command - show font selection dialog */
static void Main_OnLogFont(HWND hMainWnd)
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
static void Main_OnLogSaveAs(HWND hMainWnd)
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
static void Main_OnExit(HWND hWnd)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return;
    if (!PromptSaveIfNeeded(hWnd))
        return;
    DestroyWindow(hWnd);
}

/* About dialog procedure */
static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        {
            /* Read version info from exe */
            TCHAR szPath[MAX_PATH];
            GetModuleFileName(NULL, szPath, MAX_PATH);
            DWORD dwInfo;
            UINT size = GetFileVersionInfoSize(szPath, &dwInfo);
            if (size > 0) {
                void *pInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
                if (pInfo && GetFileVersionInfo(szPath, dwInfo, size, pInfo)) {
                    void *ptr;
                    if (VerQueryValue(pInfo, L"\\VarFileInfo\\Translation", &ptr, &size)) {
                        WORD *pLang = (WORD *)ptr;
                        TCHAR szBuf[MAX_PATH];

                        /* Product name + version */
                        swprintf(szBuf, MAX_PATH, L"\\StringFileInfo\\%04X%04X\\%ls", *pLang, *(pLang + 1), L"ProductName");
                        if (VerQueryValue(pInfo, szBuf, &ptr, &size)) {
                            void *ptr2;
                            swprintf(szBuf, MAX_PATH, L"\\StringFileInfo\\%04X%04X\\%ls", *pLang, *(pLang + 1), L"ProductVersion");
                            if (VerQueryValue(pInfo, szBuf, &ptr2, &size)) {
                                swprintf(szBuf, MAX_PATH, L"%ls v%ls", (LPTSTR)ptr, (LPTSTR)ptr2);
                                SetDlgItemText(hDlg, IDD_APPNAME, szBuf);
                            }
                        }

                        /* Copyright */
                        swprintf(szBuf, MAX_PATH, L"\\StringFileInfo\\%04X%04X\\%ls", *pLang, *(pLang + 1), L"LegalCopyright");
                        if (VerQueryValue(pInfo, szBuf, &ptr, &size)) {
                            swprintf(szBuf, MAX_PATH, L"Copyright %ls", (LPTSTR)ptr);
                            SetDlgItemText(hDlg, IDD_COPYRIGHT, szBuf);
                        }
                    }
                }
                if (pInfo) HeapFree(GetProcessHeap(), 0, pInfo);
            }
        }
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        if (((NMHDR *)lParam)->code == NM_CLICK)
            ShellExecute(NULL, L"open", ((PNMLINK)lParam)->item.szUrl, NULL, NULL, SW_SHOW);
        break;
    }
    return FALSE;
}

/* Main window procedure */
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        {
            g_hWnd = hWnd;
            HINSTANCE hInst = ((CREATESTRUCT *)lParam)->hInstance;

            /* Create toolbar */
            g_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_TOP | TBSTYLE_TOOLTIPS,
                0, 0, 0, 0, hWnd, (HMENU)IDC_MAIN_TOOLBAR, hInst, NULL);

            SendMessageW(g_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            SendMessageW(g_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));

            /* Load merged toolbar bitmap (10 icons) */
            TBADDBITMAP tbab = {0};
            tbab.hInst = hInst;
            tbab.nID = IDB_TOOLBAR;
            int iBase = (int)SendMessageW(g_hToolbar, TB_ADDBITMAP, 10, (LPARAM)&tbab);

            /* Toolbar buttons: New, Open, Save | Connect, Reconnect, Disconnect | Import, Export | Clear, SaveLog */
            TBBUTTON buttons[14] = {0};
            int btn = 0;

            /* File group */
            buttons[btn].iBitmap = iBase + 0;  /* New */
            buttons[btn].idCommand = IDM_NEW_DEVICE;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            buttons[btn].iBitmap = iBase + 1;  /* Open */
            buttons[btn].idCommand = IDM_OPEN_DEVICE;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            buttons[btn].iBitmap = iBase + 2;  /* Save */
            buttons[btn].idCommand = IDM_SAVE_DEVICE;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            /* Separator */
            buttons[btn].fsStyle = BTNS_SEP;
            btn++;

            /* Serial group */
            buttons[btn].iBitmap = iBase + 3;  /* Connect */
            buttons[btn].idCommand = IDM_CONNECT;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            buttons[btn].iBitmap = iBase + 4;  /* Reconnect */
            buttons[btn].idCommand = IDM_RECONNECT;
            buttons[btn].fsState = 0;  /* Disabled initially */
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            buttons[btn].iBitmap = iBase + 5;  /* Disconnect */
            buttons[btn].idCommand = IDM_DISCONNECT;
            buttons[btn].fsState = 0;  /* Disabled initially */
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            /* Separator */
            buttons[btn].fsStyle = BTNS_SEP;
            btn++;

            /* Flash group */
            buttons[btn].iBitmap = iBase + 6;  /* Import */
            buttons[btn].idCommand = IDM_FLASH_IMPORT;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            buttons[btn].iBitmap = iBase + 7;  /* Export */
            buttons[btn].idCommand = IDM_FLASH_EXPORT;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            /* Separator */
            buttons[btn].fsStyle = BTNS_SEP;
            btn++;

            /* Log group */
            buttons[btn].iBitmap = iBase + 8;  /* Clear */
            buttons[btn].idCommand = IDM_LOG_CLEAR;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            buttons[btn].iBitmap = iBase + 9;  /* Save Log */
            buttons[btn].idCommand = IDM_LOG_SAVEAS;
            buttons[btn].fsState = TBSTATE_ENABLED;
            buttons[btn].fsStyle = BTNS_BUTTON;
            btn++;

            SendMessageW(g_hToolbar, TB_ADDBUTTONS, btn, (LPARAM)buttons);

            /* Create status bar */
            g_hStatusbar = CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0, hWnd, (HMENU)IDC_MAIN_STATUSBAR, hInst, NULL);

            /* Create RichEdit log display */
            g_hRichEdit = LoadLibraryW(L"riched20.dll");
            g_hEdit = CreateWindowExW(0, RICHEDIT_CLASSW, NULL,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 0, 0, hWnd, (HMENU)IDC_MAIN_EDIT, hInst, NULL);

            /* Configure RichEdit: unlimited text, light gray background */
            SendMessageW(g_hEdit, EM_SETLIMITTEXT, 0, 0);
            SendMessageW(g_hEdit, EM_SETBKGNDCOLOR, 0, COLOR_BG);

            /* Initialize and apply default font */
            InitDefaultFont();
            ApplyFontToEdit(g_hEdit, &g_logFont);

            /* Register for device change notifications */
            DEV_BROADCAST_DEVICEINTERFACE dbi = {0};
            dbi.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
            dbi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            dbi.dbcc_classguid = GUID_DEVCLASS_PORTS;
            g_hDevNotify = RegisterDeviceNotificationW(hWnd, &dbi, DEVICE_NOTIFY_WINDOW_HANDLE);

            UpdateTitle(hWnd);
            UpdateMenuState(hWnd);
            UpdateStatusBar();
        }
        return 0;

    case WM_SIZE:
        {
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            SendMessageW(g_hToolbar, WM_SIZE, 0, 0);
            SendMessageW(g_hStatusbar, WM_SIZE, 0, 0);

            RECT rcToolbar, rcStatus;
            GetWindowRect(g_hToolbar, &rcToolbar);
            GetWindowRect(g_hStatusbar, &rcStatus);

            int toolbarH = rcToolbar.bottom - rcToolbar.top;
            int statusH = rcStatus.bottom - rcStatus.top;

            SetWindowPos(g_hEdit, NULL, 0, toolbarH,
                        rcClient.right, rcClient.bottom - toolbarH - statusH,
                        SWP_NOZORDER);

            UpdateStatusBar();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_NEW_DEVICE:
            if (!PromptDisconnectIfNeeded(hWnd)) {
                SetFocus(g_hEdit);
                return 0;
            }
            if (!PromptSaveIfNeeded(hWnd)) {
                SetFocus(g_hEdit);
                return 0;
            }
            if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_NEW_DEVICE), hWnd, NewDeviceDlgProc) == IDOK) {
                EsptoolProto_SetModifiedCallback(OnDeviceModified);
                Config_SetLastDeviceFile(NULL);
                UpdateStatusBar();
                UpdateTitle(hWnd);
                SetWindowTextW(g_hEdit, L"");
            }
            SetFocus(g_hEdit);
            return 0;
        case IDM_OPEN_DEVICE:
            if (!PromptDisconnectIfNeeded(hWnd)) {
                SetFocus(g_hEdit);
                return 0;
            }
            if (!PromptSaveIfNeeded(hWnd)) {
                SetFocus(g_hEdit);
                return 0;
            }
            {
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
                        EsptoolProto_SetModifiedCallback(OnDeviceModified);
                        Config_SetLastDeviceFile(szFile);
                        UpdateStatusBar();
                        UpdateTitle(hWnd);
                        SetWindowTextW(g_hEdit, L"");
                    } else {
                        MessageBoxW(hWnd, L"Failed to load device file", LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
                    }
                }
            }
            SetFocus(g_hEdit);
            return 0;
        case IDM_SAVE_DEVICE:
            {
                const WCHAR *filename = Device_GetFilename(&g_device);
                if (filename[0]) {
                    if (Device_Save(&g_device, filename)) {
                        Config_SetLastDeviceFile(filename);
                    }
                } else {
                    /* No filename, do Save As */
                    SendMessageW(hWnd, WM_COMMAND, IDM_SAVE_DEVICE_AS, 0);
                }
            }
            SetFocus(g_hEdit);
            return 0;
        case IDM_SAVE_DEVICE_AS:
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
            SetFocus(g_hEdit);
            return 0;
        case IDM_DEVICE_PROPS:
            if (Serial_IsOpen(&g_serial)) {
                MessageBoxW(hWnd, L"Disconnect serial port before modifying device properties", LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
            } else {
                if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_DEVICE_PROPS), hWnd, DevicePropsDlgProc) == IDOK) {
                    UpdateStatusBar();
                    UpdateTitle(hWnd);
                }
            }
            SetFocus(g_hEdit);
            return 0;
        case IDM_CONNECT:
            Main_OnConnect(hWnd);
            SetFocus(g_hEdit);
            return 0;
        case IDM_DISCONNECT:
            Main_OnDisconnect(hWnd);
            SetFocus(g_hEdit);
            return 0;
        case IDM_RECONNECT:
            Main_OnReconnect(hWnd);
            SetFocus(g_hEdit);
            return 0;
        case IDM_FLASH_IMPORT:
            Main_OnFlashImport(hWnd);
            SetFocus(g_hEdit);
            return 0;
        case IDM_FLASH_EXPORT:
            Main_OnFlashExport(hWnd);
            SetFocus(g_hEdit);
            return 0;
        case IDM_LOG_CLEAR:
            Main_OnLogClear(hWnd);
            return 0;
        case IDM_LOG_SAVEAS:
            Main_OnLogSaveAs(hWnd);
            SetFocus(g_hEdit);
            return 0;
        case IDM_LOG_FONT:
            Main_OnLogFont(hWnd);
            SetFocus(g_hEdit);
            return 0;
        case IDM_EXIT:
            Main_OnExit(hWnd);
            return 0;
        case IDM_ABOUT:
            DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_ABOUT), hWnd, AboutDlgProc);
            SetFocus(g_hEdit);
            return 0;
        }
        break;

    case WM_NOTIFY:
        /* Handle toolbar tooltip requests */
        if (((NMHDR *)lParam)->code == TTN_GETDISPINFOW) {
            NMTTDISPINFOW *ttt = (NMTTDISPINFOW *)lParam;
            ttt->hinst = GetModuleHandleW(NULL);
            switch (ttt->hdr.idFrom) {
            case IDM_CONNECT:
                ttt->lpszText = MAKEINTRESOURCEW(IDS_TIP_CONNECT);
                return 0;
            case IDM_DISCONNECT:
                ttt->lpszText = MAKEINTRESOURCEW(IDS_TIP_DISCONNECT);
                return 0;
            case IDM_RECONNECT:
                ttt->lpszText = MAKEINTRESOURCEW(IDS_TIP_RECONNECT);
                return 0;
            case IDM_LOG_CLEAR:
                ttt->lpszText = MAKEINTRESOURCEW(IDS_TIP_CLEAR);
                return 0;
            case IDM_LOG_SAVEAS:
                ttt->lpszText = MAKEINTRESOURCEW(IDS_TIP_SAVEAS);
                return 0;
            }
        }
        break;

    case WM_SERIAL_RX:
        /* RX Data received from serial port */
        {
            DWORD len = (DWORD)wParam;
            BYTE *data = (BYTE *)lParam;
            if (data != NULL && len > 0 && len < MAX_MSG_SIZE) {
                Main_AppendLog(hWnd, data, len, DIR_RX);
            }
            if (data != NULL) {
                HeapFree(GetProcessHeap(), 0, data);
            }
        }
        return 0;

    case WM_SERIAL_TX:
        /* TX Data sent to serial port */
        {
            DWORD len = (DWORD)wParam;
            BYTE *data = (BYTE *)lParam;
            if (data != NULL && len > 0 && len < MAX_MSG_SIZE) {
                Main_AppendLog(hWnd, data, len, DIR_TX);
            }
            if (data != NULL) {
                HeapFree(GetProcessHeap(), 0, data);
            }
        }
        return 0;

    case WM_SERIAL_ERROR:
        /* Connection lost notification from listener thread */
        TRACE_FW(TAG, "Connection lost, error code: %lu", (DWORD)wParam);
        Serial_Close(&g_serial);
        UpdateTitle(hWnd);
        UpdateMenuState(hWnd);
        UpdateStatusBar();
        MessageBoxW(hWnd, LoadStr(IDS_MSG_CONN_LOST), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return 0;

    case WM_SERIAL_LOG:
        /* Custom log message from protocol layer */
        {
            WCHAR *tag = (WCHAR *)wParam;
            WCHAR *text = (WCHAR *)lParam;
            if (tag && text) {
                Main_AppendCustomLog(hWnd, tag, text);
                HeapFree(GetProcessHeap(), 0, tag);
                HeapFree(GetProcessHeap(), 0, text);
            }
        }
        return 0;

    case WM_SERIAL_SIGNAL:
        /* Signal change notification */
        {
            DWORD param = (DWORD)wParam;
            if (param == 0) {
                /* Host signal change (DSR/CTS/RI/DCD from GetCommModemStatus) */
                DWORD modemStatus = (DWORD)lParam;
                WCHAR buf[96];
                wsprintfW(buf, L"DSR:%s CTS:%s RI:%s DCD:%s",
                          (modemStatus & MS_DSR_ON) ? L"ON" : L"OFF",
                          (modemStatus & MS_CTS_ON) ? L"ON" : L"OFF",
                          (modemStatus & MS_RING_ON) ? L"ON" : L"OFF",
                          (modemStatus & MS_RLSD_ON) ? L"ON" : L"OFF");
                Main_AppendSignalLog(L"SIG", buf, COLOR_SIGNAL);
            } else if (param == 1) {
                /* DTR change */
                BOOL state = (BOOL)lParam;
                WCHAR buf[32];
                wsprintfW(buf, L"DTR:%s", state ? L"ON" : L"OFF");
                Main_AppendSignalLog(L"SIG", buf, COLOR_SIGNAL);
            } else if (param == 2) {
                /* RTS change */
                BOOL state = (BOOL)lParam;
                WCHAR buf[32];
                wsprintfW(buf, L"RTS:%s", state ? L"ON" : L"OFF");
                Main_AppendSignalLog(L"SIG", buf, COLOR_SIGNAL);
            }
        }
        return 0;

    case WM_SERIAL_CONFIG:
        /* Configuration change notification */
        {
            DWORD baudRate = 0;
            BYTE dataBits = 0, parity = 0, stopBits = 0;
            if (Serial_GetConfig(&g_serial, &baudRate, &dataBits, &parity, &stopBits)) {
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
                WCHAR buf[64];
                wsprintfW(buf, L"%lu,%d%s%s", baudRate, dataBits, parityStr, stopStr);
                Main_AppendSignalLog(L"CFG", buf, COLOR_CONFIG);
            }
            UpdateStatusBar();
        }
        return 0;

    case WM_DEVICECHANGE:
        /* Handle device removal */
        if (wParam == DBT_DEVICEREMOVECOMPLETE && Serial_IsOpen(&g_serial)) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                TRACE_FW(TAG, "Device removed, disconnecting");
                Serial_Close(&g_serial);
                UpdateTitle(hWnd);
                UpdateMenuState(hWnd);
                UpdateStatusBar();
                MessageBoxW(hWnd, LoadStr(IDS_MSG_DEV_REMOVED),
                            LoadStr(IDS_MSG_DEV_TITLE), MB_OK | MB_ICONWARNING);
            }
        }
        break;

    case WM_CLOSE:
        /* Handle close button (X) with confirmation */
        if (!PromptDisconnectIfNeeded(hWnd))
            return 0;
        if (!PromptSaveIfNeeded(hWnd))
            return 0;
        DestroyWindow(hWnd);
        return 0;

    case WM_APP_INIT:
        /* Initialization - check for last device file */
        {
            WCHAR lastFile[MAX_PATH] = {0};
            if (Config_GetLastDeviceFile(lastFile, MAX_PATH)) {
                /* Check if file exists */
                DWORD attr = GetFileAttributesW(lastFile);
                if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    /* Prompt user */
                    WCHAR msg[MAX_PATH + 64];
                    wsprintfW(msg, L"Open last device file?\n%s", lastFile);
                    int ret = MessageBoxW(hWnd, msg, L"Open Device", MB_YESNO | MB_ICONQUESTION);
                    if (ret == IDYES) {
                        if (Device_Load(&g_device, lastFile)) {
                            EsptoolProto_SetModifiedCallback(OnDeviceModified);
                            UpdateStatusBar();
                            UpdateTitle(hWnd);
                            SetWindowTextW(g_hEdit, L"");
                            return 0;
                        }
                    }
                }
            }
            /* No last file or user declined - show New Device dialog */
            if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_NEW_DEVICE), hWnd, NewDeviceDlgProc) != IDOK) {
                /* User cancelled - exit */
                DestroyWindow(hWnd);
            } else {
                EsptoolProto_SetModifiedCallback(OnDeviceModified);
                UpdateStatusBar();
                UpdateTitle(hWnd);
            }
        }
        return 0;

    case WM_DESTROY:
        /* Unregister device notifications */
        if (g_hDevNotify) {
            UnregisterDeviceNotification(g_hDevNotify);
            g_hDevNotify = NULL;
        }
        Serial_Close(&g_serial);
        Device_Close(&g_device);
        if (g_hRichEdit) {
            FreeLibrary(g_hRichEdit);
            g_hRichEdit = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* Initialize GUI: register window class, init common controls */
static BOOL Main_Init(HINSTANCE hInstance)
{
    INITCOMMONCONTROLSEX icex = { .dwSize = sizeof(icex), .dwICC = ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    /* Initialize esptool protocol */
    EsptoolProto_Init();

    WNDCLASSEXW wc = {
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = MainWndProc,
        .hInstance = hInstance,
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
        .lpszClassName = L"FakeEsptoolClass",
        .hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP)),
    };
    RegisterClassExW(&wc);
    return TRUE;
}

/* Create and show the main application window */
static HWND Main_CreateWindow(HINSTANCE hInstance)
{
    HMENU hMenu = LoadMenuW(hInstance, MAKEINTRESOURCEW(IDR_MAIN_MENU));

    HWND hWnd = CreateWindowExW(0, L"FakeEsptoolClass", LoadStr(IDS_APP_NAME),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, hMenu, hInstance, NULL);

    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
        /* Set focus to log control for auto-scroll */
        if (g_hEdit)
            SetFocus(g_hEdit);
    }

    return hWnd;
}

/* Application entry point */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    TRACE_INIT();
    TRACE_FW(TAG, "=== FakeEsptool Started ===");

    /* Initialize configuration */
    Config_Init();

    /* Load last connected port for reconnect */
    Config_GetLastPort(g_szPort, 32);

    /* Initialize GUI subsystem */
    if (!Main_Init(hInstance)) {
        TRACE_FW(TAG, "ERROR: Main_Init failed");
        TRACE_CLOSE();
        return 1;
    }

    /* Create main application window */
    HWND hWnd = Main_CreateWindow(hInstance);
    if (!hWnd) {
        TRACE_FW(TAG, "ERROR: Main_CreateWindow failed");
        TRACE_CLOSE();
        return 1;
    }

    TRACE_FW(TAG, "Main window created: %p", hWnd);

    /* Trigger initialization - check for last device file */
    PostMessage(hWnd, WM_APP_INIT, 0, 0);

    /* Main message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    TRACE_FW(TAG, "=== FakeEsptool Exiting ===");
    TRACE_CLOSE();

    return (int)msg.wParam;
}
