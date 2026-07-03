/*
 * app_commands.c - Command handler functions
 *
 * Handles menu commands, toolbar actions, and UI state updates.
 */

#include "app_commands.h"
#include "app_logview.h"
#include "app_protocol.h"
#include "device_file.h"
#include "dlg/dlg.h"
#include "main.h"
#include "resource.h"
#include "utils/config.h"
#include "utils/lang.h"
#include "utils/trace.h"
#include <commdlg.h>
#include <richedit.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#if ENABLE_TRACE
static const char *TAG = "CMD";
#endif

/* Helper function to count set bits in a value */
static int count_set_bits(DWORD v)
{
    int count = 0;
    while (v) {
        count += v & 1;
        v >>= 1;
    }
    return count;
}

/* Encryption state and download mode for testing */
typedef enum {
    ENCRYPT_STATE_NONE = 0,
    ENCRYPT_STATE_DEV = 1,
    ENCRYPT_STATE_RELEASE = 2,
} ENCRYPT_STATE;

typedef enum {
    DOWNLOAD_MODE_NORMAL = 0,
    DOWNLOAD_MODE_SECURE = 1,
    DOWNLOAD_MODE_DISABLED = 2,
} DOWNLOAD_MODE;

static ENCRYPT_STATE g_encryptState = ENCRYPT_STATE_NONE;
static DOWNLOAD_MODE g_downloadMode = DOWNLOAD_MODE_NORMAL;

/* Status bar tooltip (using TTF_SUBRECT for per-part hit testing) */
static HWND g_hStatusTip = NULL;

#ifndef TTF_SUBRECT
#define TTF_SUBRECT 0x0010
#endif

/*
 * SetPartTooltip - Set tooltip text for a specific status bar part
 *
 * Uses SB_GETRECT to get the part's rectangle, then registers a tool
 * with TTF_SUBRECT so the tooltip triggers when the mouse is over that area.
 * Deletes and re-adds the tool to handle text and rect updates.
 */
static void SetPartTooltip(int part, const WCHAR *text)
{
    if (!g_hStatusTip || !g_hStatusbar) {
        return;
    }
    RECT rc;
    if (!SendMessageW(g_hStatusbar, SB_GETRECT, (WPARAM)part, (LPARAM)&rc)) {
        return;
    }
    TOOLINFOW ti = {0};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBRECT;
    ti.hwnd = g_hStatusbar;
    ti.uId = (UINT_PTR)part;
    ti.rect = rc;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_hStatusTip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    if (!SendMessageW(g_hStatusTip, TTM_ADDTOOLW, 0, (LPARAM)&ti)) {
        ti.cbSize = sizeof(TOOLINFOW) - sizeof(void *);
        SendMessageW(g_hStatusTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

/*
 * CreateStatusTooltip - Create balloon tooltip control for status bar
 *
 * Creates a topmost balloon tooltip with no parent (to avoid clipping issues).
 * Called once during Main_OnCreate.
 */
void CreateStatusTooltip(HWND hParent)
{
    (void)hParent;
    g_hStatusTip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | TTS_BALLOON, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL,
        GetModuleHandleW(NULL), NULL);
    if (g_hStatusTip) {
        SetWindowPos(g_hStatusTip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(g_hStatusTip, TTM_SETMAXTIPWIDTH, 0, 250);
    }
}

/*
 * PromptDisconnectIfNeeded - Check if serial is connected, prompt to disconnect
 */
BOOL PromptDisconnectIfNeeded(HWND hWnd)
{
    if (!Serial_IsOpen(&g_serial)) {
        return TRUE;
    }

    int ret =
        MessageBoxW(hWnd, LoadStr(IDS_MSG_CONFIRM_DISCONN),
                    LoadStr(IDS_MSG_DISCONN_CAP), MB_YESNO | MB_ICONQUESTION);
    if (ret != IDYES) {
        return FALSE;
    }

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
    MultiByteToWideChar(CP_UTF8, 0, g_chip.name, -1, chipName, 32);

    DWORD flashSize = g_flash.size;
    if (flashSize >= 1024 * 1024) {
        wsprintfW(buf, L"%s-%luMB", chipName, flashSize / (1024 * 1024));
    } else if (flashSize > 0) {
        wsprintfW(buf, L"%s-%luKB", chipName, flashSize / 1024);
    } else {
        lstrcpyW(buf, chipName);
    }
}

/*
 * PromptSaveIfNeeded - Check if device is modified, prompt to save
 */
BOOL PromptSaveIfNeeded(HWND hWnd)
{
    if (!g_deviceModified) {
        return TRUE;
    }

    int ret = MessageBoxW(hWnd, LoadStr(IDS_MSG_CONFIRM_SAVE),
                          LoadStr(IDS_MSG_SAVE_CAP),
                          MB_YESNOCANCEL | MB_ICONQUESTION);
    switch (ret) {
    case IDYES: {
        const WCHAR *filename = g_deviceFile;
        if (filename[0]) {
            if (DeviceFile_Save(&g_chip, &g_flash, filename)) {
                Config_SetLastDeviceFile(filename);
            } else {
                MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV),
                            LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
            ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_SAVE_DEVICE);
            if (!GetSaveFileNameW(&ofn)) {
                return FALSE;
            }
            if (DeviceFile_Save(&g_chip, &g_flash, szFile)) {
                Config_SetLastDeviceFile(szFile);
            } else {
                MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV),
                            LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    if (hPort == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    CloseHandle(hPort);
    return TRUE;
}

/*
 * CanReconnect - Check if reconnect is available
 */
BOOL CanReconnect(void)
{
    if (Serial_IsOpen(&g_serial)) {
        return FALSE;
    }
    if (!g_szPort[0]) {
        return FALSE;
    }
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
    if (g_chip.name[0]) {
        BOOL encrypted = fesp_efuse_is_flash_encryption_enabled(&g_chip);
        BOOL release = fesp_efuse_is_download_encrypt_disabled(&g_chip);
        if (encrypted && release) {
            state = ENCRYPT_STATE_RELEASE;
        } else if (encrypted) {
            state = ENCRYPT_STATE_DEV;
        } else {
            state = ENCRYPT_STATE_NONE;
        }
    }
    CheckMenuItem(hMenu, IDM_ENCRYPT_NONE,
                  state == ENCRYPT_STATE_NONE ? (MF_CHECKED | MFT_RADIOCHECK)
                                              : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_ENCRYPT_DEV,
                  state == ENCRYPT_STATE_DEV ? (MF_CHECKED | MFT_RADIOCHECK)
                                             : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_ENCRYPT_RELEASE,
                  state == ENCRYPT_STATE_RELEASE ? (MF_CHECKED | MFT_RADIOCHECK)
                                                 : MF_UNCHECKED);
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
    if (g_chip.name[0]) {
        BOOL dl_disabled = fesp_efuse_is_download_mode_disabled(&g_chip);
        BOOL secure = fesp_efuse_is_secure_download_enabled(&g_chip);
        if (dl_disabled) {
            mode = DOWNLOAD_MODE_DISABLED;
        } else if (secure) {
            mode = DOWNLOAD_MODE_SECURE;
        } else {
            mode = DOWNLOAD_MODE_NORMAL;
        }
    }
    CheckMenuItem(hMenu, IDM_DOWNLOAD_NORMAL,
                  mode == DOWNLOAD_MODE_NORMAL ? (MF_CHECKED | MFT_RADIOCHECK)
                                               : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_DOWNLOAD_SECURE,
                  mode == DOWNLOAD_MODE_SECURE ? (MF_CHECKED | MFT_RADIOCHECK)
                                               : MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_DOWNLOAD_DISABLED,
                  mode == DOWNLOAD_MODE_DISABLED ? (MF_CHECKED | MFT_RADIOCHECK)
                                                 : MF_UNCHECKED);
}

/*
 * Main_CmdEncryptState - Handle encryption state menu command
 *
 * @hWnd: Main window handle
 * @state: New encryption state (0=none, 1=dev, 2=release)
 */
void Main_CmdEncryptState(HWND hWnd, int state)
{
    g_encryptState = (ENCRYPT_STATE)state;
    if (g_chip.name[0]) {
        fesp_efuse_set_flash_encryption(&g_chip, state);
        g_deviceModified = TRUE;
    }
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
    if (g_chip.name[0]) {
        fesp_efuse_set_download_mode(&g_chip, mode);
        g_deviceModified = TRUE;
    }
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
    if (g_chip.name[0]) {
        BOOL encrypted = fesp_efuse_is_flash_encryption_enabled(&g_chip);
        BOOL release = fesp_efuse_is_download_encrypt_disabled(&g_chip);
        if (encrypted && release) {
            return IDS_ENCRYPT_RELEASE;
        }
        if (encrypted) {
            return IDS_ENCRYPT_DEV;
        }
        return IDS_ENCRYPT_NONE;
    }
    switch (g_encryptState) {
    case ENCRYPT_STATE_DEV:
        return IDS_ENCRYPT_DEV;
    case ENCRYPT_STATE_RELEASE:
        return IDS_ENCRYPT_RELEASE;
    default:
        return IDS_ENCRYPT_NONE;
    }
}

/*
 * GetDownloadModeStrId - Get string ID for current download mode
 *
 * Uses eFuse state when chip is available, falls back to manual toggle.
 */
static UINT GetDownloadModeStrId(void)
{
    if (g_chip.name[0]) {
        BOOL dl_disabled = fesp_efuse_is_download_mode_disabled(&g_chip);
        BOOL secure = fesp_efuse_is_secure_download_enabled(&g_chip);
        if (dl_disabled) {
            return IDS_DOWNLOAD_DISABLED;
        }
        if (secure) {
            return IDS_DOWNLOAD_SECURE;
        }
        return IDS_DOWNLOAD_NORMAL;
    }
    switch (g_downloadMode) {
    case DOWNLOAD_MODE_SECURE:
        return IDS_DOWNLOAD_SECURE;
    case DOWNLOAD_MODE_DISABLED:
        return IDS_DOWNLOAD_DISABLED;
    default:
        return IDS_DOWNLOAD_NORMAL;
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
    BOOL canKeyMgmt = g_chip.type != FESP_CHIP_ESP8266;

    EnableMenuItem(hMenu, IDM_CONNECT, connected ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, IDM_DISCONNECT, connected ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_RECONNECT, canReconnect ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_KEY_MGMT, canKeyMgmt ? MF_ENABLED : MF_GRAYED);

    BOOL canEfuse = g_chip.type != FESP_CHIP_ESP8266;
    EnableMenuItem(hMenu, IDM_EFUSE_IMPORT, canEfuse ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_EFUSE_EXPORT, canEfuse ? MF_ENABLED : MF_GRAYED);

    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_CONNECT, !connected);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_DISCONNECT, connected);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_RECONNECT, canReconnect);
    SendMessageW(g_hToolbar, TB_ENABLEBUTTON, IDM_KEY_MGMT, canKeyMgmt);

    /* Disable encryption and download mode menus for unsupported chips */
    BOOL canEncrypt = g_chip.type != FESP_CHIP_ESP8266;
    BOOL canDlMode = g_chip.type != FESP_CHIP_ESP8266;
    BOOL canDlSecure = canDlMode && g_chip.type != FESP_CHIP_ESP32;
    EnableMenuItem(hMenu, IDM_ENCRYPT_NONE,
                   canEncrypt ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_ENCRYPT_DEV, canEncrypt ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_ENCRYPT_RELEASE,
                   canEncrypt ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_DOWNLOAD_NORMAL,
                   canDlMode ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_DOWNLOAD_SECURE,
                   canDlSecure ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_DOWNLOAD_DISABLED,
                   canDlMode ? MF_ENABLED : MF_GRAYED);

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
    if (g_chip.name[0]) {
        WCHAR chipName[32];
        MultiByteToWideChar(CP_UTF8, 0, g_chip.name, -1, chipName, 32);
        wsprintfW(p, L" - %s", chipName);
        p += lstrlenW(p);
    }

    /* File name */
    const WCHAR *filename = g_deviceFile;
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
    if (g_deviceModified) {
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
 * Updates 6 status bar parts:
 * Part 0: Chip type + Flash size (e.g. "ESP32 4MB"), tooltip: XTAL + MAC
 * Part 1: Encryption status, tooltip: SPI_BOOT_CRYPT_CNT +
 * DIS_DOWNLOAD_MANUAL_ENCRYPT Part 2: Download mode, tooltip: DIS_DOWNLOAD_MODE
 * Part 3: Secure Boot status, tooltip: SECURE_BOOT_EN
 * Part 4: JTAG status, tooltip: DIS_PAD_JTAG
 * Part 5: Port + config (e.g. "COM10 115200,8N1")
 */
void UpdateStatusBar(void)
{
    if (!g_hStatusbar) {
        return;
    }

    int parts[6];
    parts[0] = STATUS_PART1_WIDTH;
    parts[1] = parts[0] + STATUS_PART2_WIDTH;
    parts[2] = parts[1] + STATUS_PART3_WIDTH;
    parts[3] = parts[2] + STATUS_PART4_WIDTH;
    parts[4] = parts[3] + STATUS_PART5_WIDTH;
    parts[5] = -1;
    SendMessageW(g_hStatusbar, SB_SETPARTS, 6, (LPARAM)parts);

    /* Part 1: Chip type + Flash size */
    if (g_chip.name[0]) {
        WCHAR chipName[32];
        MultiByteToWideChar(CP_UTF8, 0, g_chip.name, -1, chipName, 32);
        if (g_flash.size > 0) {
            WCHAR buf[48];
            if (g_flash.size >= 1024 * 1024) {
                wsprintfW(buf, L"%s %luMB", chipName,
                          g_flash.size / (1024 * 1024));
            } else {
                wsprintfW(buf, L"%s %luKB", chipName, g_flash.size / 1024);
            }
            SendMessageW(g_hStatusbar, SB_SETTEXT, 0, (LPARAM)buf);
        } else {
            SendMessageW(g_hStatusbar, SB_SETTEXT, 0, (LPARAM)chipName);
        }

        /* Tooltip: "40MHz AA:BB:CC:DD:EE:FF" */
        const char *xtal;
        switch (g_chip.xtal_freq) {
        case FESP_XTAL_FREQ_26M:
            xtal = "26MHz";
            break;
        case FESP_XTAL_FREQ_48M:
            xtal = "48MHz";
            break;
        case FESP_XTAL_FREQ_32M:
            xtal = "32MHz";
            break;
        default:
            xtal = "40MHz";
            break;
        }
        const BYTE *mac = fesp_chip_get_mac(&g_chip);
        WCHAR tipBuf[64];
        wsprintfW(tipBuf, L"%hs %02X:%02X:%02X:%02X:%02X:%02X", xtal, mac[0],
                  mac[1], mac[2], mac[3], mac[4], mac[5]);
        SetPartTooltip(0, tipBuf);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 0,
                     (LPARAM)LoadStr(IDS_STATUS_NO_DEVICE));
        SetPartTooltip(0, L"");
    }

    /* Part 2: Encryption status */
    if (g_chip.name[0]) {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 1,
                     (LPARAM)LoadStr(GetEncryptStateStrId()));

        /* Tooltip: eFuse field values (ESP8266 not supported) */
        if (g_chip.type == FESP_CHIP_ESP8266) {
            SetPartTooltip(1, L"");
        } else {
            DWORD crypt = fesp_efuse_get_flash_crypt_cnt(&g_chip);
            DWORD dlEnc = fesp_efuse_get_dl_encrypt_disabled(&g_chip);
            const char *f1 = "SPI_BOOT_CRYPT_CNT";
            const char *f2 = (g_chip.type == FESP_CHIP_ESP32)
                                 ? "DISABLE_DL_ENCRYPT"
                                 : "DIS_DOWNLOAD_MANUAL_ENCRYPT";
            WCHAR t1[64], t2[64];
            wsprintfW(t1, LoadStr(IDS_TIP_EFUSE_FIELD), f1, (int)crypt);
            wsprintfW(t2, LoadStr(IDS_TIP_EFUSE_FIELD), f2, (int)dlEnc);
            WCHAR tipBuf[128];
            wsprintfW(tipBuf, L"%s\n%s", t1, t2);
            SetPartTooltip(1, tipBuf);
        }
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 1, (LPARAM)L"");
        SetPartTooltip(1, L"");
    }

    /* Part 3: Download mode status */
    if (g_chip.name[0]) {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 2,
                     (LPARAM)LoadStr(GetDownloadModeStrId()));

        /* Tooltip: eFuse field value (ESP8266 not supported) */
        if (g_chip.type == FESP_CHIP_ESP8266) {
            SetPartTooltip(2, L"");
        } else {
            DWORD dlMode = fesp_efuse_get_dl_mode_disabled(&g_chip);
            const char *field;
            switch (g_chip.type) {
            case FESP_CHIP_ESP32:
                field = "UART_DOWNLOAD_DIS";
                break;
            case FESP_CHIP_ESP32S3:
                field = "DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE";
                break;
            default:
                field = "DIS_DOWNLOAD_MODE";
                break;
            }
            WCHAR tipBuf[64];
            wsprintfW(tipBuf, LoadStr(IDS_TIP_EFUSE_FIELD), field, (int)dlMode);
            SetPartTooltip(2, tipBuf);
        }
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 2, (LPARAM)L"");
        SetPartTooltip(2, L"");
    }

    /* Part 4: Secure Boot status */
    if (g_chip.name[0]) {
        BOOL sb = fesp_efuse_is_secure_boot_enabled(&g_chip);
        SendMessageW(g_hStatusbar, SB_SETTEXT, 3,
                     (LPARAM)LoadStr(sb ? IDS_SB_SECURE_BOOT_ENABLED
                                        : IDS_SB_SECURE_BOOT_DISABLED));

        /* Tooltip: eFuse field value */
        DWORD sbFlag = fesp_efuse_get_secure_boot_flag(&g_chip);
        if (g_chip.type == FESP_CHIP_ESP8266 ||
            g_chip.type == FESP_CHIP_ESP32C2) {
            SetPartTooltip(3, L"");
        } else if (g_chip.type == FESP_CHIP_ESP32) {
            WCHAR t1[64], t2[64];
            wsprintfW(t1, LoadStr(IDS_TIP_EFUSE_FIELD), "ABS_DONE_0",
                      (int)(sbFlag & 1));
            wsprintfW(t2, LoadStr(IDS_TIP_EFUSE_FIELD), "ABS_DONE_1",
                      (int)((sbFlag >> 1) & 1));
            WCHAR tipBuf[128];
            wsprintfW(tipBuf, L"%s\n%s", t1, t2);
            SetPartTooltip(3, tipBuf);
        } else {
            WCHAR tipBuf[64];
            wsprintfW(tipBuf, LoadStr(IDS_TIP_EFUSE_FIELD), "SECURE_BOOT_EN",
                      (int)sbFlag);
            SetPartTooltip(3, tipBuf);
        }
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 3, (LPARAM)L"");
        SetPartTooltip(3, L"");
    }

    /* Part 5: JTAG status */
    if (g_chip.name[0]) {
        int jtagDis = fesp_efuse_get_jtag_disabled_count(&g_chip);
        int jtagTotal = fesp_efuse_get_jtag_total_count(&g_chip);
        UINT strId;
        if (jtagTotal == 0) {
            strId = IDS_SB_JTAG_ENABLED;
        } else if (jtagDis == 0) {
            strId = IDS_SB_JTAG_ENABLED;
        } else if (jtagDis >= jtagTotal) {
            strId = IDS_SB_JTAG_DISABLED;
        } else {
            strId = IDS_SB_JTAG_PARTIAL;
        }
        SendMessageW(g_hStatusbar, SB_SETTEXT, 4, (LPARAM)LoadStr(strId));

        /* Tooltip: all JTAG eFuse fields */
        if (jtagTotal == 0) {
            SetPartTooltip(4, L"");
        } else {
            WCHAR lines[4][64];
            int n = 0;
            if (g_chip.type == FESP_CHIP_ESP32) {
                wsprintfW(lines[n++], LoadStr(IDS_TIP_EFUSE_FIELD),
                          "JTAG_DISABLE",
                          (int)fesp_efuse_get_jtag_flag(&g_chip));
            } else {
                wsprintfW(lines[n++], LoadStr(IDS_TIP_EFUSE_FIELD),
                          "DIS_PAD_JTAG",
                          (int)fesp_efuse_get_jtag_flag(&g_chip));
                wsprintfW(lines[n++], LoadStr(IDS_TIP_EFUSE_FIELD),
                          "SOFT_DIS_JTAG",
                          (int)fesp_efuse_get_soft_jtag_flag(&g_chip));
                if (jtagTotal >= 3) {
                    wsprintfW(lines[n++], LoadStr(IDS_TIP_EFUSE_FIELD),
                              "DIS_USB_JTAG",
                              (int)fesp_efuse_get_usb_jtag_flag(&g_chip));
                }
            }
            WCHAR tipBuf[192];
            lstrcpyW(tipBuf, lines[0]);
            for (int i = 1; i < n; i++) {
                lstrcatW(tipBuf, L"\n");
                lstrcatW(tipBuf, lines[i]);
            }
            SetPartTooltip(4, tipBuf);
        }
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 4, (LPARAM)L"");
        SetPartTooltip(4, L"");
    }

    /* Part 6: Serial port + config */
    if (Serial_IsOpen(&g_serial)) {
        DWORD baudRate = 115200;
        BYTE dataBits = 8, parity = NOPARITY, stopBits = ONESTOPBIT;
        Serial_GetConfig(&g_serial, &baudRate, &dataBits, &parity, &stopBits);

        const WCHAR *parityStr = L"N";
        switch (parity) {
        case NOPARITY:
            parityStr = L"N";
            break;
        case ODDPARITY:
            parityStr = L"O";
            break;
        case EVENPARITY:
            parityStr = L"E";
            break;
        case MARKPARITY:
            parityStr = L"M";
            break;
        case SPACEPARITY:
            parityStr = L"S";
            break;
        }

        const WCHAR *stopStr = L"1";
        switch (stopBits) {
        case ONESTOPBIT:
            stopStr = L"1";
            break;
        case ONE5STOPBITS:
            stopStr = L"1.5";
            break;
        case TWOSTOPBITS:
            stopStr = L"2";
            break;
        }

        WCHAR buf[48];
        wsprintfW(buf, L"%s %lu,%d%s%s", g_szPort, baudRate, dataBits,
                  parityStr, stopStr);
        SendMessageW(g_hStatusbar, SB_SETTEXT, 5, (LPARAM)buf);
    } else {
        SendMessageW(g_hStatusbar, SB_SETTEXT, 5,
                     (LPARAM)LoadStr(IDS_DISCONNECTED));
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
    cf.yHeight = plf->lfHeight * 15; /* Convert to twips (1/1440 inch) */
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
        g_logFont.lfHeight =
            -MulDiv(DEFAULT_FONT_SIZE, GetDeviceCaps(hdc, LOGPIXELSY), 72);
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
    if (!PromptDisconnectIfNeeded(hWnd)) {
        return;
    }
    if (!PromptSaveIfNeeded(hWnd)) {
        return;
    }

    /* Create default device: ESP32, 40MHz, 4MB */
    static const BYTE defaultMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    fesp_flash_close(&g_flash);
    fesp_chip_close(&g_chip);
    if (fesp_chip_init(&g_chip, FESP_CHIP_ESP32) &&
        fesp_flash_init(&g_flash, 4 * 1024 * 1024)) {
        fesp_chip_set_flash_size(&g_chip, 4 * 1024 * 1024);
        fesp_chip_set_mac(&g_chip, defaultMac);
        g_chip.xtal_freq = FESP_XTAL_FREQ_40M;
        g_deviceModified = FALSE;
        g_deviceFile[0] = 0;
        UpdateMenuState(hWnd);
        UpdateStatusBar();
        UpdateTitle(hWnd);
        SetWindowTextW(g_hEdit, L"");
    } else {
        MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_CREATE_DEV),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    if (!PromptDisconnectIfNeeded(hWnd)) {
        return;
    }
    if (!PromptSaveIfNeeded(hWnd)) {
        return;
    }
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = LoadStr(IDS_DEVICE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_OPEN_DEVICE);
    if (GetOpenFileNameW(&ofn)) {
        fesp_flash_close(&g_flash);
        fesp_chip_close(&g_chip);
        if (DeviceFile_Load(&g_chip, &g_flash, szFile)) {
            Config_SetLastDeviceFile(szFile);
            UpdateMenuState(hWnd);
            UpdateStatusBar();
            UpdateTitle(hWnd);
            SetWindowTextW(g_hEdit, L"");
        } else {
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_LOAD_DEV),
                        LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    if (!PromptDisconnectIfNeeded(hWnd)) {
        return FALSE;
    }
    if (!PromptSaveIfNeeded(hWnd)) {
        return FALSE;
    }

    fesp_flash_close(&g_flash);
    fesp_chip_close(&g_chip);
    if (DeviceFile_Load(&g_chip, &g_flash, filePath)) {
        Config_SetLastDeviceFile(filePath);
        UpdateMenuState(hWnd);
        UpdateStatusBar();
        UpdateTitle(hWnd);
        SetWindowTextW(g_hEdit, L"");
        return TRUE;
    } else {
        MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_LOAD_DEV),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    const WCHAR *filename = g_deviceFile;
    if (filename[0]) {
        if (DeviceFile_Save(&g_chip, &g_flash, filename)) {
            Config_SetLastDeviceFile(filename);
            UpdateTitle(hWnd);
        } else {
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV),
                        LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_SAVE_DEVICE_AS);
    if (GetSaveFileNameW(&ofn)) {
        if (DeviceFile_Save(&g_chip, &g_flash, szFile)) {
            Config_SetLastDeviceFile(szFile);
            UpdateTitle(hWnd);
        } else {
            MessageBoxW(hWnd, LoadStr(IDS_MSG_FAIL_SAVE_DEV),
                        LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    if (!PromptDisconnectIfNeeded(hWnd)) {
        return;
    }
    if (!PromptSaveIfNeeded(hWnd)) {
        return;
    }
    if (DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_DEVICE_PROPS),
                   hWnd, DevicePropsDlgProc) == IDOK) {
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
    if (!PromptDisconnectIfNeeded(hWnd)) {
        return;
    }
    DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_KEY_MGMT), hWnd,
               KeyMgmtDlgProc);
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
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_PORT_ERROR),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    Serial_SetReceiveCallback(&g_serial, (SERIAL_RX_CB)OnEsptoolProcessData);
    Serial_SetSignalCallback(&g_serial, (SERIAL_SIGNAL_CB)OnEsptoolSignal);
    ResetSignalState();

    /* Clear volatile eFuse regions on new connection */
    fesp_efuse_clear_volatile(&g_chip);

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
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_NO_LAST_PORT),
                    LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
        return;
    }

    if (!IsPortAvailable(g_szPort)) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_PORT_NOT_AVAIL),
                    LoadStr(IDS_MSG_WARNING), MB_OK | MB_ICONWARNING);
        return;
    }

    TRACE_FW(TAG, "Reconnecting to port: %s", g_szPort);

    if (!Serial_Open(&g_serial, g_szPort, hMainWnd)) {
        TRACE_FW(TAG, "ERROR: Serial_Open failed");
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_PORT_ERROR),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    if (!Serial_IsOpen(&g_serial)) {
        return;
    }

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
    if (!PromptDisconnectIfNeeded(hMainWnd)) {
        return;
    }
    if (!PromptSaveIfNeeded(hMainWnd)) {
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
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_IMPORT_FLASH);

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    /* Check file size */
    HANDLE hFile = CreateFileW(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_OPEN_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize != g_flash.size) {
        CloseHandle(hFile);
        WCHAR msg[128];
        wsprintfW(msg, LoadStr(IDS_MSG_FLASH_MISMATCH), fileSize, g_flash.size);
        MessageBoxW(hMainWnd, msg, LoadStr(IDS_MSG_ERROR),
                    MB_OK | MB_ICONERROR);
        return;
    }

    /* Show busy cursor and disable window */
    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    DWORD bytesRead;
    BOOL ok = ReadFile(hFile, g_flash.data, fileSize, &bytesRead, NULL) &&
              bytesRead == fileSize;
    CloseHandle(hFile);

    /* Restore window state */
    EnableWindow(hMainWnd, TRUE);
    SetCursor(hOldCursor);

    if (!ok) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_READ_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    g_deviceModified = TRUE;
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

    /* Default filename: chipname_flash.bin */
    WCHAR defName[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, g_chip.name, -1, defName, MAX_PATH);
    (void)wcscat_s(defName, MAX_PATH, L"_flash.bin");
    (void)wcscpy_s(szFile, MAX_PATH, defName);

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = LoadStr(IDS_BIN_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"bin";
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_EXPORT_FLASH);

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    /* Create snapshot of flash data */
    DWORD flashSize = g_flash.size;
    BYTE *flashSnapshot = NULL;
    if (flashSize > 0 && g_flash.data) {
        flashSnapshot = (BYTE *)HeapAlloc(GetProcessHeap(), 0, flashSize);
        if (!flashSnapshot) {
            MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC_SNAP),
                        LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
            return;
        }
        memcpy(flashSnapshot, g_flash.data, flashSize);
    }

    /* Show busy cursor and disable window */
    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    /* Write snapshot to file */
    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        EnableWindow(hMainWnd, TRUE);
        SetCursor(hOldCursor);
        HeapFree(GetProcessHeap(), 0, flashSnapshot);
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_CREATE_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD bytesWritten;
    BOOL ok = TRUE;
    if (flashSnapshot && flashSize > 0) {
        ok = WriteFile(hFile, flashSnapshot, flashSize, &bytesWritten, NULL) &&
             bytesWritten == flashSize;
    }
    CloseHandle(hFile);

    /* Restore window state */
    EnableWindow(hMainWnd, TRUE);
    SetCursor(hOldCursor);

    if (!ok) {
        DeleteFileW(szFile);
        HeapFree(GetProcessHeap(), 0, flashSnapshot);
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_WRITE_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    HeapFree(GetProcessHeap(), 0, flashSnapshot);
}

/*
 * Main_OnEfuseImport - Handle eFuse Import command
 *
 * Imports eFuse block data from .bin file (QEMU-compatible format).
 * File size must match current chip's block data size.
 * ESP8266: shows error (no block structure).
 *
 * @hMainWnd: Main window handle
 */
void Main_OnEfuseImport(HWND hMainWnd)
{
    if (g_chip.type == FESP_CHIP_ESP8266) {
        return;
    }

    int blockSize = DeviceFile_GetEfuseBlockSize(g_chip.type);
    if (blockSize <= 0) {
        return;
    }

    if (!PromptDisconnectIfNeeded(hMainWnd)) {
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
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_IMPORT_EFUSE);

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    HANDLE hFile = CreateFileW(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_OPEN_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize != (DWORD)blockSize) {
        CloseHandle(hFile);
        WCHAR msg[256];
        wsprintfW(msg, LoadStr(IDS_MSG_EFUSE_SIZE_MISMATCH), fileSize,
                  blockSize);
        MessageBoxW(hMainWnd, msg, LoadStr(IDS_MSG_ERROR),
                    MB_OK | MB_ICONERROR);
        return;
    }

    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    uint8_t *blockBuf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, fileSize);
    if (!blockBuf) {
        EnableWindow(hMainWnd, TRUE);
        SetCursor(hOldCursor);
        CloseHandle(hFile);
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD bytesRead;
    BOOL ok = ReadFile(hFile, blockBuf, fileSize, &bytesRead, NULL) &&
              bytesRead == fileSize;
    CloseHandle(hFile);

    if (ok) {
        DeviceFile_ImportEfuseBlocks(&g_chip, blockBuf, fileSize);
    }

    HeapFree(GetProcessHeap(), 0, blockBuf);
    EnableWindow(hMainWnd, TRUE);
    SetCursor(hOldCursor);

    if (!ok) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_READ_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    g_deviceModified = TRUE;
    UpdateTitle(hMainWnd);
}

/*
 * Main_OnEfuseExport - Handle eFuse Export command
 *
 * Exports eFuse block data to .bin file (QEMU-compatible format).
 * ESP8266: shows error (no block structure).
 *
 * @hMainWnd: Main window handle
 */
void Main_OnEfuseExport(HWND hMainWnd)
{
    if (g_chip.type == FESP_CHIP_ESP8266) {
        return;
    }

    int blockSize = DeviceFile_GetEfuseBlockSize(g_chip.type);
    if (blockSize <= 0) {
        return;
    }

    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = LoadStr(IDS_BIN_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"bin";

    /* Default filename: chipname_efuse.bin */
    WCHAR defName[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, g_chip.name, -1, defName, MAX_PATH);
    (void)wcscat_s(defName, MAX_PATH, L"_efuse.bin");
    (void)wcscpy_s(szFile, MAX_PATH, defName);
    ofn.lpstrFile = szFile;
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_EXPORT_EFUSE);

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    uint8_t *blockBuf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, blockSize);
    if (!blockBuf) {
        EnableWindow(hMainWnd, TRUE);
        SetCursor(hOldCursor);
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DeviceFile_ExportEfuseBlocks(&g_chip, blockBuf, blockSize);

    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, blockBuf);
        EnableWindow(hMainWnd, TRUE);
        SetCursor(hOldCursor);
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_CREATE_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD written;
    BOOL ok = WriteFile(hFile, blockBuf, blockSize, &written, NULL) &&
              written == (DWORD)blockSize;
    CloseHandle(hFile);
    HeapFree(GetProcessHeap(), 0, blockBuf);

    EnableWindow(hMainWnd, TRUE);
    SetCursor(hOldCursor);

    if (!ok) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_WRITE_FILE),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }
}

/*
 * DEVICE_SNAPSHOT - Device data snapshot for Dump Device As
 *
 * Contains copies of device data for background thread processing.
 * Thread frees this structure when done.
 */
typedef struct {
    fesp_chip_ctx_t chip;       /* Chip context snapshot */
    fesp_flash_ctx_t flash;     /* Flash context snapshot */
    WCHAR deviceFile[MAX_PATH]; /* Device file path */
    BYTE *efuse;                /* eFuse data snapshot */
    DWORD efuseSize;            /* eFuse size */
    BYTE *flashData;            /* Flash data snapshot */
    DWORD flashSize;            /* Flash size */
    WCHAR filename[MAX_PATH];   /* Output filename */
    HWND hWnd;                  /* Owner window */
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
    fwprintf(f, L"File: %ls\n",
             snap->deviceFile[0] ? snap->deviceFile : L"Untitled");
    fwprintf(f, L"Date: %04d-%02d-%02d %02d:%02d:%02d\n\n", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond);

    /* Write header info */
    fwprintf(f, L"[Header]\n");
    fwprintf(f, L"Magic:      0x%08X (\"ESP\\0\")\n", DEVICE_FILE_MAGIC);
    fwprintf(f, L"Version:    %d\n", DEVICE_FILE_VERSION);

    /* Get chip name */
    WCHAR chipName[32] = {0};
    MultiByteToWideChar(CP_UTF8, 0, snap->chip.name, -1, chipName, 32);
    fwprintf(f, L"Chip Type:  %ls\n", chipName);

    /* Get xtal freq */
    const WCHAR *xtalStr;
    switch (snap->chip.xtal_freq) {
    case FESP_XTAL_FREQ_26M:
        xtalStr = L"26MHz";
        break;
    case FESP_XTAL_FREQ_48M:
        xtalStr = L"48MHz";
        break;
    case FESP_XTAL_FREQ_32M:
        xtalStr = L"32MHz";
        break;
    default:
        xtalStr = L"40MHz";
        break;
    }
    fwprintf(f, L"XTAL Freq:  %ls\n\n", xtalStr);

    /* Write MAC address */
    fwprintf(f, L"[MAC Address]\n");
    const BYTE *mac = snap->chip.mac;
    fwprintf(f, L"%02X:%02X:%02X:%02X:%02X:%02X\n\n", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    /* Write Flash config */
    fwprintf(f, L"[Flash Config]\n");
    if (snap->flashSize >= 1024 * 1024) {
        fwprintf(f, L"Size:       %luMB (%lu bytes)\n\n",
                 snap->flashSize / (1024 * 1024), snap->flashSize);
    } else {
        fwprintf(f, L"Size:       %luKB (%lu bytes)\n\n",
                 snap->flashSize / 1024, snap->flashSize);
    }

    /* Write eFuse security fields (raw values) */
    fwprintf(f, L"[eFuse Security Fields]\n");

/* Helper to read bits from eFuse data */
#define READ_EFUSE_BITS(offset, mask)                                          \
    ((*(DWORD *)(snap->efuse + (offset)) & (mask)))

/* Count set bits in a value */
#define COUNT_BITS(v) count_set_bits(v)

    switch (snap->chip.type) {
    case FESP_CHIP_ESP32: {
        DWORD flash_crypt_cnt = READ_EFUSE_BITS(0x00, 0x7FUL << 20) >> 20;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x18, 1UL << 7) >> 7;
        DWORD dl_decrypt = READ_EFUSE_BITS(0x18, 1UL << 8) >> 8;
        DWORD dl_cache = READ_EFUSE_BITS(0x18, 1UL << 9) >> 9;
        DWORD uart_dis = READ_EFUSE_BITS(0x00, 1UL << 27) >> 27;
        fwprintf(f, L"FLASH_CRYPT_CNT:     0x%02X (%d bits set)\n",
                 flash_crypt_cnt, COUNT_BITS(flash_crypt_cnt));
        fwprintf(f, L"DISABLE_DL_ENCRYPT:  %lu\n", dl_encrypt);
        fwprintf(f, L"DISABLE_DL_DECRYPT:  %lu\n", dl_decrypt);
        fwprintf(f, L"DISABLE_DL_CACHE:    %lu\n", dl_cache);
        fwprintf(f, L"UART_DOWNLOAD_DIS:   %lu\n", uart_dis);
        break;
    }
    case FESP_CHIP_ESP32S2: {
        DWORD crypt_cnt = READ_EFUSE_BITS(0x34, 7UL << 18) >> 18;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x30, 1UL << 20) >> 20;
        DWORD dl_mode = READ_EFUSE_BITS(0x3C, 1UL << 4) >> 4;
        DWORD sec_dl = READ_EFUSE_BITS(0x3C, 1UL << 5) >> 5;
        fwprintf(f, L"SPI_BOOT_CRYPT_CNT:          0x%02X (%d bits set)\n",
                 crypt_cnt, COUNT_BITS(crypt_cnt));
        fwprintf(f, L"DIS_DOWNLOAD_MANUAL_ENCRYPT: %lu\n", dl_encrypt);
        fwprintf(f, L"DIS_DOWNLOAD_MODE:           %lu\n", dl_mode);
        fwprintf(f, L"ENABLE_SECURITY_DOWNLOAD:    %lu\n", sec_dl);
        break;
    }
    case FESP_CHIP_ESP32S3: {
        DWORD crypt_cnt = READ_EFUSE_BITS(0x34, 7UL << 18) >> 18;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x30, 1UL << 20) >> 20;
        DWORD dl_mode = READ_EFUSE_BITS(0x3C, 1UL << 4) >> 4;
        DWORD sec_dl = READ_EFUSE_BITS(0x3C, 1UL << 5) >> 5;
        fwprintf(f,
                 L"SPI_BOOT_CRYPT_CNT:                0x%02X (%d bits set)\n",
                 crypt_cnt, COUNT_BITS(crypt_cnt));
        fwprintf(f, L"DIS_DOWNLOAD_MANUAL_ENCRYPT:       %lu\n", dl_encrypt);
        fwprintf(f, L"DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE: %lu\n", dl_mode);
        fwprintf(f, L"ENABLE_SECURITY_DOWNLOAD:          %lu\n", sec_dl);
        break;
    }
    case FESP_CHIP_ESP32C2: {
        DWORD crypt_cnt = READ_EFUSE_BITS(0x30, 7UL << 7) >> 7;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x30, 1UL << 6) >> 6;
        DWORD dl_mode = READ_EFUSE_BITS(0x30, 1UL << 14) >> 14;
        DWORD sec_dl = READ_EFUSE_BITS(0x30, 1UL << 16) >> 16;
        fwprintf(f, L"SPI_BOOT_CRYPT_CNT:          0x%02X (%d bits set)\n",
                 crypt_cnt, COUNT_BITS(crypt_cnt));
        fwprintf(f, L"DIS_DOWNLOAD_MANUAL_ENCRYPT: %lu\n", dl_encrypt);
        fwprintf(f, L"DIS_DOWNLOAD_MODE:           %lu\n", dl_mode);
        fwprintf(f, L"ENABLE_SECURITY_DOWNLOAD:    %lu\n", sec_dl);
        break;
    }
    case FESP_CHIP_ESP32C3:
    case FESP_CHIP_ESP32C6:
    case FESP_CHIP_ESP32H2:
    case FESP_CHIP_ESP32P4: {
        DWORD crypt_cnt = READ_EFUSE_BITS(0x34, 7UL << 18) >> 18;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x30, 1UL << 20) >> 20;
        DWORD dl_mode = READ_EFUSE_BITS(0x3C, 1UL << 0) >> 0;
        DWORD sec_dl = READ_EFUSE_BITS(0x3C, 1UL << 5) >> 5;
        fwprintf(f,
                 L"SPI_BOOT_CRYPT_CNT:                0x%02X (%d bits set)\n",
                 crypt_cnt, COUNT_BITS(crypt_cnt));
        fwprintf(f, L"DIS_DOWNLOAD_MANUAL_ENCRYPT:       %lu\n", dl_encrypt);
        fwprintf(f, L"DIS_DOWNLOAD_MODE:                 %lu\n", dl_mode);
        fwprintf(f, L"ENABLE_SECURITY_DOWNLOAD:          %lu\n", sec_dl);
        break;
    }
    case FESP_CHIP_ESP32C5: {
        DWORD crypt_cnt = READ_EFUSE_BITS(0x34, 7UL << 16) >> 16;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x30, 1UL << 20) >> 20;
        DWORD dl_mode = READ_EFUSE_BITS(0x3C, 1UL << 0) >> 0;
        DWORD sec_dl = READ_EFUSE_BITS(0x3C, 1UL << 5) >> 5;
        fwprintf(f,
                 L"SPI_BOOT_CRYPT_CNT:                0x%02X (%d bits set)\n",
                 crypt_cnt, COUNT_BITS(crypt_cnt));
        fwprintf(f, L"DIS_DOWNLOAD_MANUAL_ENCRYPT:       %lu\n", dl_encrypt);
        fwprintf(f, L"DIS_DOWNLOAD_MODE:                 %lu\n", dl_mode);
        fwprintf(f, L"ENABLE_SECURITY_DOWNLOAD:          %lu\n", sec_dl);
        break;
    }
    case FESP_CHIP_ESP32S31: {
        DWORD crypt_cnt = READ_EFUSE_BITS(0x34, 7UL << 21) >> 21;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x30, 1UL << 20) >> 20;
        DWORD dl_mode = READ_EFUSE_BITS(0x3C, 1UL << 0) >> 0;
        DWORD sec_dl = READ_EFUSE_BITS(0x3C, 1UL << 5) >> 5;
        fwprintf(f,
                 L"SPI_BOOT_CRYPT_CNT:                0x%02X (%d bits set)\n",
                 crypt_cnt, COUNT_BITS(crypt_cnt));
        fwprintf(f, L"DIS_DOWNLOAD_MANUAL_ENCRYPT:       %lu\n", dl_encrypt);
        fwprintf(f, L"DIS_DOWNLOAD_MODE:                 %lu\n", dl_mode);
        fwprintf(f, L"ENABLE_SECURITY_DOWNLOAD:          %lu\n", sec_dl);
        break;
    }
    case FESP_CHIP_ESP32C61: {
        DWORD crypt_cnt = READ_EFUSE_BITS(0x30, 7UL << 23) >> 23;
        DWORD dl_encrypt = READ_EFUSE_BITS(0x30, 1UL << 14) >> 14;
        DWORD dl_mode = READ_EFUSE_BITS(0x3C, 1UL << 0) >> 0;
        DWORD sec_dl = READ_EFUSE_BITS(0x3C, 1UL << 5) >> 5;
        fwprintf(f,
                 L"SPI_BOOT_CRYPT_CNT:                0x%02X (%d bits set)\n",
                 crypt_cnt, COUNT_BITS(crypt_cnt));
        fwprintf(f, L"DIS_DOWNLOAD_MANUAL_ENCRYPT:       %lu\n", dl_encrypt);
        fwprintf(f, L"DIS_DOWNLOAD_MODE:                 %lu\n", dl_mode);
        fwprintf(f, L"ENABLE_SECURITY_DOWNLOAD:          %lu\n", sec_dl);
        break;
    }
    default:
        fwprintf(f, L"(not available for this chip)\n");
        break;
    }

    fwprintf(f, L"\n");

    /* Write JTAG and Secure Boot fields */
    fwprintf(f, L"[JTAG & Secure Boot]\n");

    switch (snap->chip.type) {
    case FESP_CHIP_ESP32: {
        DWORD jtag_dis = READ_EFUSE_BITS(0x18, 1UL << 6) >> 6;
        DWORD abs_done0 = READ_EFUSE_BITS(0x18, 1UL << 4) >> 4;
        DWORD abs_done1 = READ_EFUSE_BITS(0x18, 1UL << 5) >> 5;
        fwprintf(f, L"JTAG_DISABLE:    %lu\n", jtag_dis);
        fwprintf(f, L"ABS_DONE_0:      %lu (Secure Boot V1)\n", abs_done0);
        fwprintf(f, L"ABS_DONE_1:      %lu (Secure Boot V2)\n", abs_done1);
        break;
    }
    case FESP_CHIP_ESP32S2: {
        DWORD pad_jtag = READ_EFUSE_BITS(0x30, 1UL << 19) >> 19;
        DWORD soft_jtag = READ_EFUSE_BITS(0x30, 1UL << 17) >> 17;
        DWORD force_dl = READ_EFUSE_BITS(0x30, 1UL << 12) >> 12;
        DWORD sb_en = READ_EFUSE_BITS(0x38, 1UL << 20) >> 20;
        DWORD sb_agg = READ_EFUSE_BITS(0x38, 1UL << 21) >> 21;
        DWORD revoke0 = READ_EFUSE_BITS(0x34, 1UL << 21) >> 21;
        DWORD revoke1 = READ_EFUSE_BITS(0x34, 1UL << 22) >> 22;
        DWORD revoke2 = READ_EFUSE_BITS(0x34, 1UL << 23) >> 23;
        fwprintf(f, L"DIS_PAD_JTAG:               %lu\n", pad_jtag);
        fwprintf(f, L"SOFT_DIS_JTAG:              %lu\n", soft_jtag);
        fwprintf(f, L"DIS_FORCE_DOWNLOAD:         %lu\n", force_dl);
        fwprintf(f, L"SECURE_BOOT_EN:             %lu\n", sb_en);
        fwprintf(f, L"SECURE_BOOT_AGGRESSIVE:     %lu\n", sb_agg);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE0:    %lu\n", revoke0);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE1:    %lu\n", revoke1);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE2:    %lu\n", revoke2);
        break;
    }
    case FESP_CHIP_ESP32S3: {
        DWORD pad_jtag = READ_EFUSE_BITS(0x30, 1UL << 19) >> 19;
        DWORD soft_jtag = (READ_EFUSE_BITS(0x30, 7UL << 16) >> 16);
        DWORD usb_jtag = READ_EFUSE_BITS(0x38, 1UL << 22) >> 22;
        DWORD force_dl = READ_EFUSE_BITS(0x30, 1UL << 12) >> 12;
        DWORD usb_print = READ_EFUSE_BITS(0x3C, 1UL << 2) >> 2;
        DWORD sb_en = READ_EFUSE_BITS(0x38, 1UL << 20) >> 20;
        DWORD sb_agg = READ_EFUSE_BITS(0x38, 1UL << 21) >> 21;
        DWORD revoke0 = READ_EFUSE_BITS(0x34, 1UL << 21) >> 21;
        DWORD revoke1 = READ_EFUSE_BITS(0x34, 1UL << 22) >> 22;
        DWORD revoke2 = READ_EFUSE_BITS(0x34, 1UL << 23) >> 23;
        fwprintf(f, L"DIS_PAD_JTAG:               %lu\n", pad_jtag);
        fwprintf(f, L"SOFT_DIS_JTAG:              %lu (%d bits set)\n",
                 soft_jtag, COUNT_BITS(soft_jtag));
        fwprintf(f, L"DIS_USB_JTAG:               %lu\n", usb_jtag);
        fwprintf(f, L"DIS_FORCE_DOWNLOAD:         %lu\n", force_dl);
        fwprintf(f, L"DIS_USB_SERIAL_JTAG_PRINT:  %lu\n", usb_print);
        fwprintf(f, L"SECURE_BOOT_EN:             %lu\n", sb_en);
        fwprintf(f, L"SECURE_BOOT_AGGRESSIVE:     %lu\n", sb_agg);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE0:    %lu\n", revoke0);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE1:    %lu\n", revoke1);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE2:    %lu\n", revoke2);
        break;
    }
    case FESP_CHIP_ESP32C2: {
        DWORD force_dl = READ_EFUSE_BITS(0x30, 1UL << 14) >> 14;
        fwprintf(f, L"DIS_FORCE_DOWNLOAD:         %lu\n", force_dl);
        fwprintf(f, L"(JTAG and Secure Boot not supported)\n");
        break;
    }
    case FESP_CHIP_ESP32C3:
    case FESP_CHIP_ESP32C6:
    case FESP_CHIP_ESP32H2: {
        DWORD pad_jtag = READ_EFUSE_BITS(0x30, 1UL << 19) >> 19;
        DWORD soft_jtag = (READ_EFUSE_BITS(0x30, 7UL << 16) >> 16);
        DWORD usb_jtag = READ_EFUSE_BITS(0x30, 1UL << 9) >> 9;
        DWORD force_dl = READ_EFUSE_BITS(0x30, 1UL << 12) >> 12;
        DWORD usb_print = READ_EFUSE_BITS(0x3C, 1UL << 2) >> 2;
        DWORD sb_en = READ_EFUSE_BITS(0x38, 1UL << 20) >> 20;
        DWORD sb_agg = READ_EFUSE_BITS(0x38, 1UL << 21) >> 21;
        DWORD revoke0 = READ_EFUSE_BITS(0x34, 1UL << 21) >> 21;
        DWORD revoke1 = READ_EFUSE_BITS(0x34, 1UL << 22) >> 22;
        DWORD revoke2 = READ_EFUSE_BITS(0x34, 1UL << 23) >> 23;
        fwprintf(f, L"DIS_PAD_JTAG:               %lu\n", pad_jtag);
        fwprintf(f, L"SOFT_DIS_JTAG:              %lu (%d bits set)\n",
                 soft_jtag, COUNT_BITS(soft_jtag));
        fwprintf(f, L"DIS_USB_JTAG:               %lu\n", usb_jtag);
        fwprintf(f, L"DIS_FORCE_DOWNLOAD:         %lu\n", force_dl);
        fwprintf(f, L"DIS_USB_SERIAL_JTAG_PRINT:  %lu\n", usb_print);
        fwprintf(f, L"SECURE_BOOT_EN:             %lu\n", sb_en);
        fwprintf(f, L"SECURE_BOOT_AGGRESSIVE:     %lu\n", sb_agg);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE0:    %lu\n", revoke0);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE1:    %lu\n", revoke1);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE2:    %lu\n", revoke2);
        break;
    }
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32C61: {
        DWORD pad_jtag = READ_EFUSE_BITS(0x30, 1UL << 19) >> 19;
        DWORD soft_jtag = (READ_EFUSE_BITS(0x30, 7UL << 16) >> 16);
        DWORD usb_jtag = READ_EFUSE_BITS(0x30, 1UL << 9) >> 9;
        DWORD force_dl = READ_EFUSE_BITS(0x30, 1UL << 12) >> 12;
        DWORD usb_print = READ_EFUSE_BITS(0x3C, 1UL << 2) >> 2;
        DWORD sb_en_c5 = (g_chip.type == FESP_CHIP_ESP32C5)
                             ? READ_EFUSE_BITS(0x38, 1UL << 25) >> 25
                             : READ_EFUSE_BITS(0x34, 1UL << 26) >> 26;
        DWORD sb_agg = READ_EFUSE_BITS(0x38, 1UL << 21) >> 21;
        DWORD revoke0 = READ_EFUSE_BITS(0x34, 1UL << 21) >> 21;
        DWORD revoke1 = READ_EFUSE_BITS(0x34, 1UL << 22) >> 22;
        DWORD revoke2 = READ_EFUSE_BITS(0x34, 1UL << 23) >> 23;
        fwprintf(f, L"DIS_PAD_JTAG:               %lu\n", pad_jtag);
        fwprintf(f, L"SOFT_DIS_JTAG:              %lu (%d bits set)\n",
                 soft_jtag, COUNT_BITS(soft_jtag));
        fwprintf(f, L"DIS_USB_JTAG:               %lu\n", usb_jtag);
        fwprintf(f, L"DIS_FORCE_DOWNLOAD:         %lu\n", force_dl);
        fwprintf(f, L"DIS_USB_SERIAL_JTAG_PRINT:  %lu\n", usb_print);
        fwprintf(f, L"SECURE_BOOT_EN:             %lu\n", sb_en_c5);
        fwprintf(f, L"SECURE_BOOT_AGGRESSIVE:     %lu\n", sb_agg);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE0:    %lu\n", revoke0);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE1:    %lu\n", revoke1);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE2:    %lu\n", revoke2);
        break;
    }
    case FESP_CHIP_ESP32P4: {
        DWORD sb_en = READ_EFUSE_BITS(0x38, 1UL << 20) >> 20;
        fwprintf(f, L"SECURE_BOOT_EN:             %lu\n", sb_en);
        fwprintf(f, L"(JTAG fields not available)\n");
        break;
    }
    case FESP_CHIP_ESP32S31: {
        DWORD pad_jtag = READ_EFUSE_BITS(0x30, 1UL << 19) >> 19;
        DWORD soft_jtag = (READ_EFUSE_BITS(0x30, 7UL << 16) >> 16);
        DWORD usb_jtag = READ_EFUSE_BITS(0x30, 1UL << 9) >> 9;
        DWORD force_dl = READ_EFUSE_BITS(0x30, 1UL << 12) >> 12;
        DWORD usb_print = READ_EFUSE_BITS(0x3C, 1UL << 2) >> 2;
        DWORD sb_en = READ_EFUSE_BITS(0x3C, 1UL << 2) >> 2;
        DWORD sb_agg = READ_EFUSE_BITS(0x38, 1UL << 21) >> 21;
        DWORD revoke0 = READ_EFUSE_BITS(0x34, 1UL << 21) >> 21;
        DWORD revoke1 = READ_EFUSE_BITS(0x34, 1UL << 22) >> 22;
        DWORD revoke2 = READ_EFUSE_BITS(0x34, 1UL << 23) >> 23;
        fwprintf(f, L"DIS_PAD_JTAG:               %lu\n", pad_jtag);
        fwprintf(f, L"SOFT_DIS_JTAG:              %lu (%d bits set)\n",
                 soft_jtag, COUNT_BITS(soft_jtag));
        fwprintf(f, L"DIS_USB_JTAG:               %lu\n", usb_jtag);
        fwprintf(f, L"DIS_FORCE_DOWNLOAD:         %lu\n", force_dl);
        fwprintf(f, L"DIS_USB_SERIAL_JTAG_PRINT:  %lu\n", usb_print);
        fwprintf(f, L"SECURE_BOOT_EN:             %lu\n", sb_en);
        fwprintf(f, L"SECURE_BOOT_AGGRESSIVE:     %lu\n", sb_agg);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE0:    %lu\n", revoke0);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE1:    %lu\n", revoke1);
        fwprintf(f, L"SECURE_BOOT_KEY_REVOKE2:    %lu\n", revoke2);
        break;
    }
    default:
        fwprintf(f, L"(not available for this chip)\n");
        break;
    }

#undef READ_EFUSE_BITS
#undef COUNT_BITS

    fwprintf(f, L"\n");

    /* Write key management info */
    fwprintf(f, L"[Key Management]\n");

    /* Get key blocks for current chip type */
    typedef struct {
        const char *name;
        int offset;
        int size;
    } KEY_INFO;

    const KEY_INFO *keys = NULL;
    int key_count = 0;

    switch (snap->chip.type) {
    case FESP_CHIP_ESP32: {
        static const KEY_INFO esp32_keys[] = {
            {"BLOCK1", 0x38, 32},
            {"BLOCK2", 0x58, 32},
            {"BLOCK3", 0x78, 32},
        };
        keys = esp32_keys;
        key_count = 3;
        break;
    }
    case FESP_CHIP_ESP32S2:
    case FESP_CHIP_ESP32S3: {
        static const KEY_INFO s2s3_keys[] = {
            {"KEY0", 0x9C, 32}, {"KEY1", 0xBC, 32},  {"KEY2", 0xDC, 32},
            {"KEY3", 0xFC, 32}, {"KEY4", 0x11C, 32}, {"KEY5", 0x13C, 32},
        };
        keys = s2s3_keys;
        key_count = 6;
        break;
    }
    case FESP_CHIP_ESP32C2: {
        static const KEY_INFO c2_keys[] = {
            {"KEY0", 0x60, 32},
        };
        keys = c2_keys;
        key_count = 1;
        break;
    }
    case FESP_CHIP_ESP32C3:
    case FESP_CHIP_ESP32C6:
    case FESP_CHIP_ESP32C5:
    case FESP_CHIP_ESP32C61:
    case FESP_CHIP_ESP32H2:
    case FESP_CHIP_ESP32P4: {
        static const KEY_INFO c3c6_keys[] = {
            {"KEY0", 0x9C, 32}, {"KEY1", 0xBC, 32},  {"KEY2", 0xDC, 32},
            {"KEY3", 0xFC, 32}, {"KEY4", 0x11C, 32}, {"KEY5", 0x13C, 32},
        };
        keys = c3c6_keys;
        key_count = 6;
        break;
    }
    case FESP_CHIP_ESP32S31: {
        static const KEY_INFO s31_keys[] = {
            {"KEY0", 0x9C, 32}, {"KEY1", 0xBC, 32},  {"KEY2", 0xDC, 32},
            {"KEY3", 0xFC, 32}, {"KEY4", 0x11C, 32},
        };
        keys = s31_keys;
        key_count = 5;
        break;
    }
    default:
        break;
    }

    fwprintf(f, L"%-10ls %-24ls %-10ls %-10ls\n", L"Block", L"Purpose",
             L"Status", L"Size");
    fwprintf(f, L"---------- ------------------------ ---------- ----------\n");

    for (int i = 0; i < key_count; i++) {
        /* Check if key is programmed (non-zero) */
        BOOL programmed = FALSE;
        if (keys[i].offset + keys[i].size <= (int)snap->efuseSize) {
            for (int j = 0; j < keys[i].size; j++) {
                if (snap->efuse[keys[i].offset + j] != 0) {
                    programmed = TRUE;
                    break;
                }
            }
        }

        /* Get actual KEY_PURPOSE from eFuse */
        BYTE purpose = fesp_efuse_get_key_purpose(&snap->chip, i);
        const WCHAR *purposeStr;
        switch (purpose) {
        case FESP_KEY_PURPOSE_USER:
            purposeStr = L"USER (0)";
            break;
        case FESP_KEY_PURPOSE_RESERVED:
            purposeStr = L"RESERVED (1)";
            break;
        case FESP_KEY_PURPOSE_XTS_AES_256_KEY_1:
            purposeStr = L"XTS-AES-256-1 (2)";
            break;
        case FESP_KEY_PURPOSE_XTS_AES_256_KEY_2:
            purposeStr = L"XTS-AES-256-2 (3)";
            break;
        case FESP_KEY_PURPOSE_XTS_AES_128_KEY:
            purposeStr = L"XTS-AES-128 (4)";
            break;
        case FESP_KEY_PURPOSE_HMAC_DOWN_ALL:
            purposeStr = L"HMAC-DOWN-ALL (5)";
            break;
        case FESP_KEY_PURPOSE_HMAC_DOWN_JTAG:
            purposeStr = L"HMAC-DOWN-JTAG (6)";
            break;
        case FESP_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE:
            purposeStr = L"HMAC-DOWN-SIG (7)";
            break;
        case FESP_KEY_PURPOSE_HMAC_UP:
            purposeStr = L"HMAC-UP (8)";
            break;
        case FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST0:
            purposeStr = L"SEC-BOOT-DIG0 (9)";
            break;
        case FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST1:
            purposeStr = L"SEC-BOOT-DIG1 (10)";
            break;
        case FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST2:
            purposeStr = L"SEC-BOOT-DIG2 (11)";
            break;
        default:
            purposeStr = L"UNKNOWN";
            break;
        }

        WCHAR wname[16];
        MultiByteToWideChar(CP_UTF8, 0, keys[i].name, -1, wname, 16);

        fwprintf(f, L"%-10ls %-24ls %-10ls %-10ls\n", wname, purposeStr,
                 programmed ? L"Programmed" : L"Empty",
                 keys[i].size == 32 ? L"256-bit" : L"128-bit");
    }

    fwprintf(f, L"\n");

    /* Write eFuse data */
    fwprintf(f, L"[eFuse] (%lu bytes)\n", snap->efuseSize);
    fwprintf(
        f,
        L"Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII\n");
    fwprintf(f, L"--------  -----------------------  -----------------------  "
                L"----------------\n");

    for (DWORD i = 0; i < snap->efuseSize; i += 16) {
        fwprintf(f, L"%08X  ", i);
        /* Hex bytes */
        for (DWORD j = 0; j < 16; j++) {
            if (i + j < snap->efuseSize) {
                fwprintf(f, L"%02X ", snap->efuse[i + j]);
            } else {
                fwprintf(f, L"   ");
            }
            if (j == 7) {
                fwprintf(f, L" ");
            }
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
    fwprintf(
        f,
        L"Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII\n");
    fwprintf(f, L"--------  -----------------------  -----------------------  "
                L"----------------\n");

    for (DWORD i = 0; i < snap->flashSize; i += 16) {
        fwprintf(f, L"%08X  ", i);
        /* Hex bytes */
        for (DWORD j = 0; j < 16; j++) {
            if (i + j < snap->flashSize) {
                fwprintf(f, L"%02X ", snap->flashData[i + j]);
            } else {
                fwprintf(f, L"   ");
            }
            if (j == 7) {
                fwprintf(f, L" ");
            }
        }
        fwprintf(f, L" ");
        /* ASCII */
        for (DWORD j = 0; j < 16 && (i + j) < snap->flashSize; j++) {
            BYTE ch = snap->flashData[i + j];
            fwprintf(f, L"%c", (ch >= 32 && ch < 127) ? (WCHAR)ch : L'.');
        }
        fwprintf(f, L"\n");
    }

cleanup:
    if (f) {
        fclose(f);
    }

    /* Notify main window */
    PostMessage(snap->hWnd, WM_DUMP_COMPLETE, ok ? TRUE : FALSE,
                ok ? 0 : GetLastError());

    /* Free snapshot data */
    if (snap->efuse) {
        HeapFree(GetProcessHeap(), 0, snap->efuse);
    }
    if (snap->flashData) {
        HeapFree(GetProcessHeap(), 0, snap->flashData);
    }
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
    const WCHAR *devName = g_deviceFile;
    if (devName[0]) {
        /* Extract name without path and extension */
        const WCHAR *name = wcsrchr(devName, L'\\');
        name = name ? name + 1 : devName;
        lstrcpyW(szFile, name);
        /* Remove .esp extension */
        WCHAR *ext = wcsrchr(szFile, L'.');
        if (ext) {
            *ext = L'\0';
        }
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
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_DUMP_DEVICE);

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    /* Create snapshot */
    DEVICE_SNAPSHOT *snap = (DEVICE_SNAPSHOT *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DEVICE_SNAPSHOT));
    if (!snap) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    /* Copy device info */
    snap->chip = g_chip;
    snap->flash = g_flash;
    (void)wcscpy_s(snap->deviceFile, MAX_PATH, g_deviceFile);
    snap->hWnd = hMainWnd;
    lstrcpyW(snap->filename, szFile);

    /* Snapshot eFuse data */
    snap->efuseSize = g_chip.efuse_size;
    if (snap->efuseSize > 0 && g_chip.efuse) {
        snap->efuse = (BYTE *)HeapAlloc(GetProcessHeap(), 0, snap->efuseSize);
        if (!snap->efuse) {
            HeapFree(GetProcessHeap(), 0, snap);
            MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC_EFUSE),
                        LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
            return;
        }
        memcpy(snap->efuse, g_chip.efuse, snap->efuseSize);
    }

    /* Snapshot Flash data */
    snap->flashSize = g_flash.size;
    if (snap->flashSize > 0 && g_flash.data) {
        snap->flashData =
            (BYTE *)HeapAlloc(GetProcessHeap(), 0, snap->flashSize);
        if (!snap->flashData) {
            if (snap->efuse) {
                HeapFree(GetProcessHeap(), 0, snap->efuse);
            }
            HeapFree(GetProcessHeap(), 0, snap);
            MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_ALLOC_FLASH),
                        LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
            return;
        }
        memcpy(snap->flashData, g_flash.data, snap->flashSize);
    }

    /* Show busy cursor and disable window */
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    EnableWindow(hMainWnd, FALSE);

    /* Start dump thread */
    HANDLE hThread = CreateThread(NULL, 0, DumpThreadProc, snap, 0, NULL);
    if (!hThread) {
        EnableWindow(hMainWnd, TRUE);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        if (snap->efuse) {
            HeapFree(GetProcessHeap(), 0, snap->efuse);
        }
        if (snap->flashData) {
            HeapFree(GetProcessHeap(), 0, snap->flashData);
        }
        HeapFree(GetProcessHeap(), 0, snap);
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_FAIL_DUMP_THREAD),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
    if (g_hEdit) {
        SetWindowTextW(g_hEdit, L"");
    }
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
    cf.Flags = CF_SCREENFONTS | CF_FIXEDPITCHONLY | CF_INITTOLOGFONTSTRUCT |
               CF_NOVERTFONTS;
    cf.nFontType = SCREEN_FONTTYPE;

    if (ChooseFontW(&cf)) {
        g_logFont = lf;
        ApplyFontToEdit(g_hEdit, &g_logFont);
        Config_SetFont(&g_logFont); /* Save to config */
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
    wsprintfW(szFile, L"FakeEsptool_%04d%02d%02d_%02d%02d%02d.log", st.wYear,
              st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = LoadStr(IDS_LOG_SAVE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"log";
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_SAVE_LOG);

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hMainWnd, LoadStr(IDS_MSG_SAVE_ERROR),
                    LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
        return;
    }

    int textLen = GetWindowTextLengthW(g_hEdit);
    if (textLen > 0) {
        WCHAR *buf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
                                        (textLen + 1) * sizeof(WCHAR));
        if (buf) {
            GetWindowTextW(g_hEdit, buf, textLen + 1);

            /* Convert to UTF-8 (no BOM) */
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf, textLen + 1,
                                              NULL, 0, NULL, NULL);
            if (utf8Len > 0) {
                char *utf8Buf = (char *)HeapAlloc(GetProcessHeap(), 0, utf8Len);
                if (utf8Buf) {
                    WideCharToMultiByte(CP_UTF8, 0, buf, textLen + 1, utf8Buf,
                                        utf8Len, NULL, NULL);
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
    if (!PromptDisconnectIfNeeded(hWnd)) {
        return;
    }
    if (!PromptSaveIfNeeded(hWnd)) {
        return;
    }
    DestroyWindow(hWnd);
}
