/*
 * main.c - FakeEsptool Application
 *
 * Main entry point, GUI implementation with toolbar, status bar,
 * and RichEdit log display. Handles menu commands and serial port events.
 */

#include <windows.h>
#include "main.h"
#include "serial.h"
#include "esptool/esptool.h"
#include "esptool/device.h"
#include "resource.h"
#include "utils/config.h"
#include "utils/lang.h"
#include "utils/trace.h"
#include "dlg/dlg.h"
#include "app_logview.h"
#include "app_commands.h"
#include <richedit.h>
#include <commdlg.h>
#include <dbt.h>
#include <devguid.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winver.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Version.lib")

#pragma comment(lib, "comctl32.lib")

#if ENABLE_TRACE
static const char *TAG = "GUI";
#endif

/* Populate flash size combo box based on chip selection */
static const WCHAR *esp8266_flash_names[] = { L"256KB", L"512KB", L"1MB", L"2MB", L"4MB", L"8MB", L"16MB" };
static const DWORD  esp8266_flash_sizes[] = { 256*1024, 512*1024, 1024*1024, 2*1024*1024, 4*1024*1024, 8*1024*1024, 16*1024*1024 };
#define ESP8266_FLASH_COUNT 7
#define ESP8266_FLASH_DEFAULT 4  /* 4MB */

static const WCHAR *esp32_flash_names[] = { L"1MB", L"2MB", L"4MB", L"8MB", L"16MB", L"32MB", L"64MB", L"128MB" };
static const DWORD  esp32_flash_sizes[] = { 1024*1024, 2*1024*1024, 4*1024*1024, 8*1024*1024, 16*1024*1024, 32*1024*1024, 64*1024*1024, 128*1024*1024 };
#define ESP32_FLASH_COUNT 8
#define ESP32_FLASH_DEFAULT 2  /* 4MB */

/*
 * PopulateFlashSizes - Populate flash size combo box
 *
 * Fills the combo box with flash size options based on chip type.
 * ESP8266 has different size options than ESP32 family.
 *
 * @hFlash:     Handle to combo box control
 * @chip:       Chip type enum
 * @currentSize: Current flash size to select
 */
void PopulateFlashSizes(HWND hFlash, CHIP_TYPE chip, DWORD currentSize)
{
    SendMessageW(hFlash, CB_RESETCONTENT, 0, 0);

    BOOL isEsp8266 = (chip == CHIP_ESP8266);
    const WCHAR **names = isEsp8266 ? esp8266_flash_names : esp32_flash_names;
    const DWORD  *sizes = isEsp8266 ? esp8266_flash_sizes : esp32_flash_sizes;
    int count = isEsp8266 ? ESP8266_FLASH_COUNT : ESP32_FLASH_COUNT;
    int defaultIdx = isEsp8266 ? ESP8266_FLASH_DEFAULT : ESP32_FLASH_DEFAULT;

    int selectIdx = defaultIdx;
    for (int i = 0; i < count; i++) {
        SendMessageW(hFlash, CB_ADDSTRING, 0, (LPARAM)names[i]);
        if (sizes[i] == currentSize)
            selectIdx = i;
    }
    SendMessageW(hFlash, CB_SETCURSEL, selectIdx, 0);
}

/*
 * GetFlashSizeFromCombo - Get flash size from combo box selection
 *
 * Returns the flash size in bytes based on current combo box selection.
 *
 * @hFlash: Handle to combo box control
 * @chip:   Chip type enum (determines size table)
 *
 * Returns flash size in bytes.
 */
DWORD GetFlashSizeFromCombo(HWND hFlash, CHIP_TYPE chip)
{
    int sel = (int)SendMessageW(hFlash, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    BOOL isEsp8266 = (chip == CHIP_ESP8266);
    const DWORD *sizes = isEsp8266 ? esp8266_flash_sizes : esp32_flash_sizes;
    int count = isEsp8266 ? ESP8266_FLASH_COUNT : ESP32_FLASH_COUNT;
    if (sel >= count) sel = count - 1;
    return sizes[sel];
}

/* Global state */
SERIAL_CTX g_serial = { .hPort = NULL, .hThread = NULL, .hStartEvent = NULL, .hNotify = NULL, .bRunning = FALSE };
DEVICE_CTX g_device = {0};
ESPTOOL_CTX g_esptool = {0};
static HWND g_hWnd = NULL;           /* Main window handle */
HWND g_hToolbar = NULL;
HWND g_hStatusbar = NULL;
HWND g_hEdit = NULL;
static HMODULE g_hRichEdit = NULL;   /* RichEdit DLL handle */
static HDEVNOTIFY g_hDevNotify = NULL;  /* Device notification handle */
WCHAR g_szPort[32] = {0};
WCHAR g_szSelectedPort[32] = {0};
LOGFONTW g_logFont = {0};  /* Current font */

/* Forward declarations */
static LRESULT Main_OnCreate(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnSize(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnNotify(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnSerialRx(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnSerialTx(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnSerialError(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnSerialLog(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnSerialSignal(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnSerialConfig(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnDumpComplete(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnDeviceChange(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnClose(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnAppInit(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnDestroy(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnCopyData(HWND hWnd, WPARAM wParam, LPARAM lParam);
static LRESULT Main_OnDropFiles(HWND hWnd, WPARAM wParam, LPARAM lParam);

/*
 * OnDeviceModified - Callback when device data is modified by protocol
 */
void OnDeviceModified(void)
{
    Device_SetModified(&g_device, TRUE);
    if (g_hWnd)
        UpdateTitle(g_hWnd);
}

/*
 * OnSerialWrite - Callback to write data to serial port
 */
static DWORD OnSerialWrite(const BYTE *data, DWORD len)
{
    if (!Serial_IsOpen(&g_serial))
        return 0;
    return Serial_WriteData(&g_serial, data, len, g_hWnd);
}

/*
 * OnBaudRateChange - Callback to change serial port baud rate
 */
static BOOL OnBaudRateChange(DWORD baudRate)
{
    if (!Serial_IsOpen(&g_serial))
        return FALSE;
    return Serial_SetBaudRate(&g_serial, baudRate);
}

/*
 * OnEsptoolProcessData - esptool protocol data receive callback
 */
void OnEsptoolProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify)
{
    if (!ctx || !data || len == 0)
        return;
    g_esptool.hNotify = hNotify;
    Esptool_Feed(&g_esptool, data, (int)len);
}

/* Signal state for download mode detection */
static BOOL g_prev_dsr = FALSE;
static BOOL g_prev_cts = FALSE;
static BOOL g_reset_pending = FALSE;
static BOOL g_saw_io0_low = FALSE;  /* DSR:ON CTS:OFF seen = IO0=LOW */

/*
 * ResetSignalState - Reset signal state (call when serial connection is established)
 */
void ResetSignalState(void)
{
    g_prev_dsr = FALSE;
    g_prev_cts = FALSE;
    g_reset_pending = FALSE;
    g_saw_io0_low = FALSE;
}

/*
 * OutputBootMessage - Output boot message to serial and log window (for download mode)
 */
static void OutputBootMessage(SERIAL_CTX *ctx, BOOL download_mode, BYTE reset_cause, HWND hNotify)
{
    Esptool_ResetState(&g_esptool);

    DWORD bootBaud = Chip_GetBootBaudRate(&g_device.chip);
    Serial_SetBaudRate(ctx, bootBaud);
    Serial_PostLogF(hNotify, L"CFG", L"Baud rate: %lu", bootBaud);

    const char *msg = Chip_GetBootMessage(&g_device.chip, download_mode, reset_cause);
    if (msg[0]) {
        Serial_WriteData(ctx, (const BYTE *)msg, (DWORD)strlen(msg), hNotify);

        const char *line = msg;
        while (*line) {
            const char *end = strchr(line, '\r');
            if (!end) end = line + strlen(line);
            int wlen = MultiByteToWideChar(CP_UTF8, 0, line, (int)(end - line), NULL, 0);
            if (wlen > 0) {
                WCHAR *wline = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (wlen + 1) * sizeof(WCHAR));
                if (wline) {
                    MultiByteToWideChar(CP_UTF8, 0, line, (int)(end - line), wline, wlen);
                    wline[wlen] = L'\0';
                    Serial_PostLog(hNotify, L"BOOT", wline);
                    HeapFree(GetProcessHeap(), 0, wline);
                }
            }
            line = end;
            while (*line == '\r' || *line == '\n') line++;
        }
    }

    /* Download mode: switch special chips (74880) to 115200 for esptool communication */
    if (download_mode && bootBaud != 115200) {
        Serial_SetBaudRate(ctx, 115200);
        Serial_PostLogF(hNotify, L"CFG", L"Baud rate: 115200");
    }
}

/*
 * OnEsptoolSignal - esptool protocol signal change callback
 *
 * Detects ClassicReset sequence (DTR/RTS) to enter download mode.
 *
 * Signal mapping (active-low logic):
 *   DTR -> GPIO0: DTR=ON -> GPIO0=LOW (select download mode)
 *   RTS -> EN:    RTS=ON -> EN=LOW (reset chip)
 *
 * ClassicReset sequence:
 *   Step 1: DTR=OFF, RTS=ON  -> DSR=OFF, CTS=ON  (EN=LOW, GPIO0=HIGH)
 *   Step 2: DTR=ON,  RTS=OFF -> DSR=ON,  CTS=OFF (GPIO0=LOW, EN=HIGH)
 *   Step 3: DTR=OFF, RTS=OFF -> DSR=OFF, CTS=OFF (release, enter mode)
 *
 * State transition table:
 *   DSR  CTS  g_reset_pending  g_saw_io0_low  Action
 *   OFF  ON   FALSE            -              Start reset, set pending
 *   ON   OFF  TRUE             FALSE          Set IO0 low flag
 *   ON   ON   TRUE             -              Intermediate (ignore)
 *   OFF  OFF  TRUE             TRUE           ClassicReset: download mode
 *   OFF  OFF  TRUE             FALSE          HardReset: normal boot
 *   Other combinations                      Cancel pending reset
 *
 * Note: Called only from the serial listener thread (single-threaded access)
 */
void OnEsptoolSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify)
{
    BOOL dsr = (modemStatus & MS_DSR_ON) != 0;
    BOOL cts = (modemStatus & MS_CTS_ON) != 0;

    if (dsr != g_prev_dsr || cts != g_prev_cts) {
        Serial_PostLogF(hNotify, L"SIG", L"DSR:%s CTS:%s",
                        dsr ? L"ON" : L"OFF", cts ? L"ON" : L"OFF");

        /* DSR:OFF CTS:ON = Reset start (DTR=OFF, RTS=ON -> IO0=HIGH, EN=LOW) */
        if (!dsr && cts) {
            g_reset_pending = TRUE;
            g_saw_io0_low = FALSE;
        }
        /* DSR:ON CTS:OFF = IO0=LOW (DTR=ON, RTS=OFF -> GPIO0=LOW) */
        else if (g_reset_pending && dsr && !cts) {
            g_saw_io0_low = TRUE;
        }
        /* DSR:ON CTS:ON = Intermediate state during ClassicReset (ignore) */
        else if (g_reset_pending && dsr && cts) {
            /* Keep g_reset_pending and g_saw_io0_low unchanged */
        }
        /* DSR:OFF CTS:OFF = Reset end */
        else if (g_reset_pending && !dsr && !cts) {
            if (g_saw_io0_low) {
                /* ClassicReset: IO0 was LOW -> enter download mode */
                Serial_PostLog(hNotify, L"SIG", L"Download mode entered");
                OutputBootMessage(ctx, TRUE, 0x01, hNotify);  /* download, POWERON */
            } else {
                /* HardReset: IO0 stayed HIGH -> normal boot */
                Serial_PostLog(hNotify, L"SIG", L"Hard reset (normal boot)");
                OutputBootMessage(ctx, FALSE, 0x02, hNotify);  /* normal boot, EXT */
            }
            g_reset_pending = FALSE;
            g_saw_io0_low = FALSE;
        }
        /* Any other state cancels pending reset */
        else {
            g_reset_pending = FALSE;
            g_saw_io0_low = FALSE;
        }

        g_prev_dsr = dsr;
        g_prev_cts = cts;
    }
}

/*
 * MainWndProc - Main window procedure
 *
 * Handles all window messages for the application main window.
 * Dispatches messages to specific handler functions.
 */
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:         return Main_OnCreate(hWnd, wParam, lParam);
    case WM_SIZE:           return Main_OnSize(hWnd, wParam, lParam);
    case WM_COMMAND:        return Main_OnCommand(hWnd, wParam, lParam);
    case WM_NOTIFY:         return Main_OnNotify(hWnd, wParam, lParam);
    case WM_TIMER:
        if (wParam == LOG_FLUSH_TIMER_ID) {
            LogView_FlushTimer();
            return 0;
        }
        break;
    case WM_SERIAL_RX:      return Main_OnSerialRx(hWnd, wParam, lParam);
    case WM_SERIAL_TX:      return Main_OnSerialTx(hWnd, wParam, lParam);
    case WM_SERIAL_ERROR:   return Main_OnSerialError(hWnd, wParam, lParam);
    case WM_SERIAL_LOG:     return Main_OnSerialLog(hWnd, wParam, lParam);
    case WM_SERIAL_SIGNAL:  return Main_OnSerialSignal(hWnd, wParam, lParam);
    case WM_SERIAL_CONFIG:  return Main_OnSerialConfig(hWnd, wParam, lParam);
    case WM_DUMP_COMPLETE:  return Main_OnDumpComplete(hWnd, wParam, lParam);
    case WM_DEVICECHANGE:   return Main_OnDeviceChange(hWnd, wParam, lParam);
    case WM_CLOSE:          return Main_OnClose(hWnd, wParam, lParam);
    case WM_APP_INIT:       return Main_OnAppInit(hWnd, wParam, lParam);
    case WM_DESTROY:        return Main_OnDestroy(hWnd, wParam, lParam);
    case WM_COPYDATA:       return Main_OnCopyData(hWnd, wParam, lParam);
    case WM_DROPFILES:      return Main_OnDropFiles(hWnd, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/*
 * Main_OnCreate - Handle WM_CREATE
 *
 * Creates toolbar, status bar, and edit control.
 */
static LRESULT Main_OnCreate(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    g_hWnd = hWnd;
    Esptool_SetNotify(&g_esptool, g_hWnd);
    HINSTANCE hInst = ((CREATESTRUCT *)lParam)->hInstance;

    /* Create toolbar */
    g_hToolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_TOP | TBSTYLE_TOOLTIPS,
        0, 0, 0, 0, hWnd, (HMENU)IDC_MAIN_TOOLBAR, hInst, NULL);

    SendMessageW(g_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));

    /* Load merged toolbar bitmap (11 icons) */
    TBADDBITMAP tbab = {0};
    tbab.hInst = hInst;
    tbab.nID = IDB_TOOLBAR;
    int iBase = (int)SendMessageW(g_hToolbar, TB_ADDBITMAP, 11, (LPARAM)&tbab);

    /* Toolbar buttons: New, Open, Save | DeviceProps | Connect, Reconnect, Disconnect | Import, Export | Clear, SaveLog */
    TBBUTTON buttons[16] = {0};
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

    buttons[btn].iBitmap = iBase + 3;  /* Device Properties */
    buttons[btn].idCommand = IDM_DEVICE_PROPS;
    buttons[btn].fsState = TBSTATE_ENABLED;
    buttons[btn].fsStyle = BTNS_BUTTON;
    btn++;

    /* Separator */
    buttons[btn].fsStyle = BTNS_SEP;
    btn++;

    /* Serial group */
    buttons[btn].iBitmap = iBase + 4;  /* Connect */
    buttons[btn].idCommand = IDM_CONNECT;
    buttons[btn].fsState = TBSTATE_ENABLED;
    buttons[btn].fsStyle = BTNS_BUTTON;
    btn++;

    buttons[btn].iBitmap = iBase + 5;  /* Reconnect */
    buttons[btn].idCommand = IDM_RECONNECT;
    buttons[btn].fsState = 0;  /* Disabled initially */
    buttons[btn].fsStyle = BTNS_BUTTON;
    btn++;

    buttons[btn].iBitmap = iBase + 6;  /* Disconnect */
    buttons[btn].idCommand = IDM_DISCONNECT;
    buttons[btn].fsState = 0;  /* Disabled initially */
    buttons[btn].fsStyle = BTNS_BUTTON;
    btn++;

    /* Separator */
    buttons[btn].fsStyle = BTNS_SEP;
    btn++;

    /* Flash group */
    buttons[btn].iBitmap = iBase + 7;  /* Import */
    buttons[btn].idCommand = IDM_FLASH_IMPORT;
    buttons[btn].fsState = TBSTATE_ENABLED;
    buttons[btn].fsStyle = BTNS_BUTTON;
    btn++;

    buttons[btn].iBitmap = iBase + 8;  /* Export */
    buttons[btn].idCommand = IDM_FLASH_EXPORT;
    buttons[btn].fsState = TBSTATE_ENABLED;
    buttons[btn].fsStyle = BTNS_BUTTON;
    btn++;

    /* Separator */
    buttons[btn].fsStyle = BTNS_SEP;
    btn++;

    /* Log group */
    buttons[btn].iBitmap = iBase + 9;  /* Clear */
    buttons[btn].idCommand = IDM_LOG_CLEAR;
    buttons[btn].fsState = TBSTATE_ENABLED;
    buttons[btn].fsStyle = BTNS_BUTTON;
    btn++;

    buttons[btn].iBitmap = iBase + 10;  /* Save Log */
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

    /* Initialize log view subsystem (buffered batch updates) */
    LogView_Init(hWnd);

    /* Register for device change notifications */
    DEV_BROADCAST_DEVICEINTERFACE dbi = {0};
    dbi.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    dbi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    dbi.dbcc_classguid = GUID_DEVCLASS_PORTS;
    g_hDevNotify = RegisterDeviceNotificationW(hWnd, &dbi, DEVICE_NOTIFY_WINDOW_HANDLE);

    UpdateTitle(hWnd);
    UpdateMenuState(hWnd);
    UpdateStatusBar();

    /* Enable drag and drop */
    DragAcceptFiles(hWnd, TRUE);

    return 0;
}

/*
 * Main_OnSize - Handle WM_SIZE
 *
 * Resizes toolbar, status bar, and edit control.
 */
static LRESULT Main_OnSize(HWND hWnd, WPARAM wParam, LPARAM lParam)
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
    return 0;
}

/*
 * Main_OnCommand - Handle WM_COMMAND
 *
 * Dispatches menu and toolbar commands.
 */
static LRESULT Main_OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    switch (LOWORD(wParam)) {
    case IDM_NEW_DEVICE:     Main_CmdNewDevice(hWnd); break;
    case IDM_OPEN_DEVICE:    Main_CmdOpenDevice(hWnd); break;
    case IDM_SAVE_DEVICE:    Main_CmdSaveDevice(hWnd); break;
    case IDM_SAVE_DEVICE_AS: Main_CmdSaveDeviceAs(hWnd); break;
    case IDM_DEVICE_PROPS:   Main_CmdDeviceProps(hWnd); break;
    case IDM_CONNECT:        Main_OnConnect(hWnd); break;
    case IDM_DISCONNECT:     Main_OnDisconnect(hWnd); break;
    case IDM_RECONNECT:      Main_OnReconnect(hWnd); break;
    case IDM_FLASH_IMPORT:   Main_OnFlashImport(hWnd); break;
    case IDM_FLASH_EXPORT:   Main_OnFlashExport(hWnd); break;
    case IDM_DUMP_DEVICE_AS: Main_OnDumpDeviceAs(hWnd); break;
    case IDM_LOG_CLEAR:      Main_OnLogClear(hWnd); break;
    case IDM_LOG_SAVEAS:     Main_OnLogSaveAs(hWnd); break;
    case IDM_LOG_FONT:       Main_OnLogFont(hWnd); break;
    case IDM_EXIT:           Main_OnExit(hWnd); break;
    case IDM_ABOUT:
        DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_ABOUT), hWnd, AboutDlgProc);
        break;
    }
    SetFocus(g_hEdit);
    return 0;
}

/*
 * Main_OnNotify - Handle WM_NOTIFY
 *
 * Handles toolbar tooltip requests.
 */
static LRESULT Main_OnNotify(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    if (((NMHDR *)lParam)->code == TTN_GETDISPINFOW) {
        NMTTDISPINFOW *ttt = (NMTTDISPINFOW *)lParam;
        ttt->hinst = GetModuleHandleW(NULL);
        switch (ttt->hdr.idFrom) {
        case IDM_DEVICE_PROPS:
            ttt->lpszText = MAKEINTRESOURCEW(IDS_TIP_DEVPROPS);
            return 0;
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
    return 0;
}

/*
 * Main_OnSerialRx - Handle WM_SERIAL_RX
 *
 * RX data received from serial port.
 */
static LRESULT Main_OnSerialRx(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    DWORD len = (DWORD)wParam;
    BYTE *data = (BYTE *)lParam;
    if (data != NULL && len > 0 && len < MAX_MSG_SIZE) {
        Main_AppendLog(hWnd, data, len, DIR_RX);
    }
    if (data != NULL) {
        HeapFree(GetProcessHeap(), 0, data);
    }
    return 0;
}

/*
 * Main_OnSerialTx - Handle WM_SERIAL_TX
 *
 * TX data sent to serial port.
 */
static LRESULT Main_OnSerialTx(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    DWORD len = (DWORD)wParam;
    BYTE *data = (BYTE *)lParam;
    if (data != NULL && len > 0 && len < MAX_MSG_SIZE) {
        Main_AppendLog(hWnd, data, len, DIR_TX);
    }
    if (data != NULL) {
        HeapFree(GetProcessHeap(), 0, data);
    }
    return 0;
}

/*
 * Main_OnSerialError - Handle WM_SERIAL_ERROR
 *
 * Connection lost notification.
 */
static LRESULT Main_OnSerialError(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    TRACE_FW(TAG, "Connection lost, error code: %lu", (DWORD)wParam);
    Serial_Close(&g_serial);
    UpdateTitle(hWnd);
    UpdateMenuState(hWnd);
    UpdateStatusBar();
    MessageBoxW(hWnd, LoadStr(IDS_MSG_CONN_LOST), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
    return 0;
}

/*
 * Main_OnSerialLog - Handle WM_SERIAL_LOG
 *
 * Custom log message from protocol layer.
 */
static LRESULT Main_OnSerialLog(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    WCHAR *tag = (WCHAR *)wParam;
    WCHAR *text = (WCHAR *)lParam;
    if (tag && text) {
        Main_AppendCustomLog(hWnd, tag, text);
        HeapFree(GetProcessHeap(), 0, tag);
        HeapFree(GetProcessHeap(), 0, text);
    }
    return 0;
}

/*
 * Main_OnSerialSignal - Handle WM_SERIAL_SIGNAL
 *
 * Signal change notification.
 */
static LRESULT Main_OnSerialSignal(HWND hWnd, WPARAM wParam, LPARAM lParam)
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
    return 0;
}

/*
 * Main_OnSerialConfig - Handle WM_SERIAL_CONFIG
 *
 * Configuration change notification.
 */
static LRESULT Main_OnSerialConfig(HWND hWnd, WPARAM wParam, LPARAM lParam)
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
    return 0;
}

/*
 * Main_OnDumpComplete - Handle WM_DUMP_COMPLETE
 *
 * Dump thread completion notification.
 */
static LRESULT Main_OnDumpComplete(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    BOOL success = (BOOL)wParam;
    DWORD errorCode = (DWORD)lParam;

    /* Restore window state */
    EnableWindow(hWnd, TRUE);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    SetFocus(g_hEdit);

    if (!success) {
        WCHAR msg[128];
        wsprintfW(msg, LoadStr(IDS_MSG_FAIL_DUMP), errorCode);
        MessageBoxW(hWnd, msg, LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
    }
    return 0;
}

/*
 * Main_OnDeviceChange - Handle WM_DEVICECHANGE
 *
 * Device removal notification.
 */
static LRESULT Main_OnDeviceChange(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
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
    return 0;
}

/*
 * Main_OnClose - Handle WM_CLOSE
 *
 * Close button (X) with confirmation.
 */
static LRESULT Main_OnClose(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    if (!PromptDisconnectIfNeeded(hWnd))
        return 0;
    if (!PromptSaveIfNeeded(hWnd))
        return 0;
    DestroyWindow(hWnd);
    return 0;
}

/*
 * Main_OnAppInit - Handle WM_APP_INIT
 *
 * Initialization, check for command line file or last device file.
 */
static LRESULT Main_OnAppInit(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;

    /* Check for command line file path */
    const WCHAR *cmdFilePath = (const WCHAR *)lParam;
    if (cmdFilePath && cmdFilePath[0]) {
        TRACE_FW(TAG, "Opening command line file: %ls", cmdFilePath);
        if (Device_Load(&g_device, cmdFilePath)) {
            Esptool_SetModifiedCallback(&g_esptool, OnDeviceModified);
            Config_SetLastDeviceFile(cmdFilePath);
            UpdateStatusBar();
            UpdateTitle(hWnd);
            SetWindowTextW(g_hEdit, L"");
            return 0;
        } else {
            TRACE_FW(TAG, "Failed to load command line file");
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_LOAD_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        }
    }

    /* No command line file - check for last device file */
    WCHAR lastFile[MAX_PATH] = {0};
    if (Config_GetLastDeviceFile(lastFile, MAX_PATH)) {
        /* Check if file exists */
        DWORD attr = GetFileAttributesW(lastFile);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            /* Prompt user */
            WCHAR msg[MAX_PATH + 64];
            wsprintfW(msg, LoadStr(IDS_MSG_OPEN_LAST_FILE), lastFile);
            int ret = MessageBoxW(hWnd, msg, LoadStr(IDS_OPEN_DEVICE_TITLE), MB_YESNO | MB_ICONQUESTION);
            if (ret == IDYES) {
                if (Device_Load(&g_device, lastFile)) {
                    Esptool_SetModifiedCallback(&g_esptool, OnDeviceModified);
                    UpdateStatusBar();
                    UpdateTitle(hWnd);
                    SetWindowTextW(g_hEdit, L"");
                    return 0;
                }
            }
        }
    }
    /* No last file or user declined - create default device */
    {
        static const BYTE defaultMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
        if (Device_Init(&g_device, CHIP_ESP32, 4 * 1024 * 1024, defaultMac)) {
            g_device.chip.xtal_freq = XTAL_FREQ_40M;
            Esptool_SetModifiedCallback(&g_esptool, OnDeviceModified);
            UpdateStatusBar();
            UpdateTitle(hWnd);
        } else {
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_CREATE_DEV), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
            DestroyWindow(hWnd);
        }
    }
    return 0;
}

/*
 * Main_OnCopyData - Handle WM_COPYDATA
 *
 * Receive file path from another instance.
 */
static LRESULT Main_OnCopyData(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;
    if (pcds && pcds->lpData && pcds->cbData >= 2 * sizeof(WCHAR)) {
        const WCHAR *filePath = (const WCHAR *)pcds->lpData;
        /* Ensure null termination and valid string */
        size_t maxLen = pcds->cbData / sizeof(WCHAR);
        if (filePath[maxLen - 1] == L'\0' || filePath[maxLen - 2] == L'\0') {
            /* Activate window */
            if (IsIconic(hWnd))
                ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            /* Open file */
            Main_OpenDeviceFile(hWnd, filePath);
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * Main_OnDropFiles - Handle WM_DROPFILES
 *
 * File drag and drop.
 */
static LRESULT Main_OnDropFiles(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    HDROP hDrop = (HDROP)wParam;
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

    if (fileCount == 0) {
        DragFinish(hDrop);
        return 0;
    }

    /* Get first file path */
    WCHAR filePath[MAX_PATH] = {0};
    DragQueryFileW(hDrop, 0, filePath, MAX_PATH);

    /* Check if it's an .esp file */
    WCHAR *ext = wcsrchr(filePath, L'.');
    if (!ext || _wcsicmp(ext, L".esp") != 0) {
        DragFinish(hDrop);
        MessageBoxW(hWnd, LoadStr(IDS_MSG_ONLY_ESP),
                    LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
        return 0;
    }

    /* Prompt user */
    BOOL openFile = FALSE;
    if (fileCount == 1) {
        WCHAR msg[MAX_PATH + 64];
        wsprintfW(msg, LoadStr(IDS_MSG_OPEN_FILE), filePath);
        openFile = (MessageBoxW(hWnd, msg, LoadStr(IDS_OPEN_DEVICE_TITLE),
                               MB_YESNO | MB_ICONQUESTION) == IDYES);
    } else {
        WCHAR msg[MAX_PATH + 128];
        wsprintfW(msg, LoadStr(IDS_MSG_OPEN_MULTI_FILE), filePath);
        openFile = (MessageBoxW(hWnd, msg, LoadStr(IDS_OPEN_DEVICE_TITLE),
                               MB_YESNO | MB_ICONQUESTION) == IDYES);
    }

    if (openFile) {
        Main_OpenDeviceFile(hWnd, filePath);
    }

    DragFinish(hDrop);
    return 0;
}

/*
 * Main_OnDestroy - Handle WM_DESTROY
 *
 * Cleanup and exit.
 */
static LRESULT Main_OnDestroy(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    /* Flush and shutdown log view subsystem */
    LogView_Close();

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

/*
 * Main_Init - Initialize GUI subsystem
 *
 * Registers window class, initializes common controls, and sets up
 * esptool protocol callbacks.
 *
 * @hInstance: Application instance handle
 *
 * Returns TRUE on success.
 */
static BOOL Main_Init(HINSTANCE hInstance)
{
    INITCOMMONCONTROLSEX icex = { .dwSize = sizeof(icex), .dwICC = ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    /* Initialize esptool protocol with pointers to device data */
    Esptool_Init(&g_esptool, &g_device.chip, &g_device.flash);
    Esptool_SetWriteCallback(&g_esptool, OnSerialWrite);
    Esptool_SetBaudRateCallback(&g_esptool, OnBaudRateChange);

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

/*
 * Main_CreateWindow - Create and show main application window
 *
 * Creates the main window with menu and shows it.
 *
 * @hInstance: Application instance handle
 *
 * Returns window handle, or NULL on failure.
 */
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

/*
 * wWinMain - Application entry point
 *
 * Initializes the application, handles single instance check,
 * parses command line, creates main window, and runs message loop.
 *
 * @hInstance:     Current instance handle
 * @hPrevInstance: Previous instance handle (unused)
 * @lpCmdLine:     Command line string
 * @nCmdShow:      Window show state (unused)
 *
 * Returns exit code from message loop.
 */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)nCmdShow;

    srand((unsigned)time(NULL));

    TRACE_INIT();
    TRACE_FW(TAG, "=== FakeEsptool Started ===");

    /* Parse command line for file path */
    WCHAR cmdFilePath[MAX_PATH] = {0};
    if (lpCmdLine && lpCmdLine[0]) {
        /* Remove surrounding quotes if present */
        const WCHAR *src = lpCmdLine;
        if (src[0] == L'"') {
            src++;
            const WCHAR *end = wcschr(src, L'"');
            if (end) {
                size_t len = end - src;
                if (len < MAX_PATH) {
                    wcsncpy(cmdFilePath, src, len);
                    cmdFilePath[len] = L'\0';
                }
            }
        } else {
            /* Take first argument (until space or end) */
            size_t len = wcslen(src);
            if (len >= MAX_PATH) len = MAX_PATH - 1;
            wcsncpy(cmdFilePath, src, len);
            cmdFilePath[len] = L'\0';
        }
        /* Convert to full path */
        if (cmdFilePath[0]) {
            WCHAR fullPath[MAX_PATH] = {0};
            if (GetFullPathNameW(cmdFilePath, MAX_PATH, fullPath, NULL)) {
                wcscpy(cmdFilePath, fullPath);
            }
        }
    }

    /* Single instance check */
    HANDLE hMutex = CreateMutexW(NULL, TRUE, SINGLE_INSTANCE_MUTEX);
    DWORD mutexError = GetLastError();

    if (mutexError == ERROR_ALREADY_EXISTS) {
        /* Another instance is running */
        WaitForSingleObject(hMutex, 2000);

        HWND hExistingWnd = FindWindowW(L"FakeEsptoolClass", NULL);
        if (hExistingWnd) {
            if (cmdFilePath[0]) {
                /* Prompt user */
                WCHAR msg[MAX_PATH + 128];
                wsprintfW(msg, LoadStr(IDS_MSG_ALREADY_RUNNING), cmdFilePath);
                if (MessageBoxW(NULL, msg, LoadStr(IDS_APP_NAME), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    /* Send file path to existing instance */
                    COPYDATASTRUCT cds = {0};
                    cds.dwData = 0;
                    cds.cbData = (DWORD)((wcslen(cmdFilePath) + 1) * sizeof(WCHAR));
                    cds.lpData = (void *)cmdFilePath;
                    SendMessageW(hExistingWnd, WM_COPYDATA, 0, (LPARAM)&cds);
                }
            } else {
                /* Just activate existing window */
                if (IsIconic(hExistingWnd))
                    ShowWindow(hExistingWnd, SW_RESTORE);
                SetForegroundWindow(hExistingWnd);
            }
        }

        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        TRACE_FW(TAG, "Another instance already running, exiting");
        TRACE_CLOSE();
        return 0;
    }

    /* Initialize configuration */
    Config_Init();

    /* Load last connected port for reconnect */
    Config_GetLastPort(g_szPort, 32);

    /* Initialize GUI subsystem */
    if (!Main_Init(hInstance)) {
        TRACE_FW(TAG, "ERROR: Main_Init failed");
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        TRACE_CLOSE();
        return 1;
    }

    /* Create main application window */
    HWND hWnd = Main_CreateWindow(hInstance);
    if (!hWnd) {
        TRACE_FW(TAG, "ERROR: Main_CreateWindow failed");
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        TRACE_CLOSE();
        return 1;
    }

    TRACE_FW(TAG, "Main window created: %p", hWnd);

    /* Load accelerator table */
    HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_MAIN_ACCEL));

    /* Store command line file path for WM_APP_INIT to process */
    static WCHAR s_cmdFilePath[MAX_PATH] = {0};
    if (cmdFilePath[0]) {
        /* Check if file exists */
        DWORD attr = GetFileAttributesW(cmdFilePath);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            wcscpy(s_cmdFilePath, cmdFilePath);
        } else {
            TRACE_FW(TAG, "Command line file not found: %ls", cmdFilePath);
            MessageBoxW(NULL, LoadStr(IDS_MSG_FILE_NOT_FOUND), LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        }
    }

    /* Trigger initialization - check for last device file or command line file */
    PostMessage(hWnd, WM_APP_INIT, 0, s_cmdFilePath[0] ? (LPARAM)s_cmdFilePath : 0);

    /* Main message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(hWnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    TRACE_FW(TAG, "=== FakeEsptool Exiting ===");

    /* Cleanup mutex */
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    TRACE_CLOSE();

    return (int)msg.wParam;
}
