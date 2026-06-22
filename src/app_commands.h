/*
 * app_commands.h - Command handler functions
 */

#ifndef APP_COMMANDS_H
#define APP_COMMANDS_H

#include <windows.h>
#include "serial.h"
#include "esptool/device.h"
#include "esptool/esptool.h"
#include "utils/config.h"
#include "utils/lang.h"
#include "utils/trace.h"
#include "dlg/dlg.h"

/* Global state (defined in main.c) */
extern HWND g_hEdit;
extern DEVICE_CTX g_device;
extern ESPTOOL_CTX g_esptool;
extern SERIAL_CTX g_serial;
extern WCHAR g_szPort[32];
extern WCHAR g_szSelectedPort[32];
extern LOGFONTW g_logFont;
extern HWND g_hToolbar;
extern HWND g_hStatusbar;

/* Default font settings */
#define DEFAULT_FONT_NAME L"Consolas"
#define DEFAULT_FONT_SIZE 10

/* Status bar part widths */
#define STATUS_PART1_WIDTH  100  /* Chip + Flash size */
#define STATUS_PART2_WIDTH  110  /* Encryption status */
#define STATUS_PART3_WIDTH  110  /* Download mode */
#define STATUS_PART4_WIDTH  110  /* Secure Boot status */
#define STATUS_PART5_WIDTH  100  /* JTAG status */

/* Message size limit */
#define MAX_MSG_SIZE        65536

/*
 * OnEsptoolProcessData - esptool protocol data receive callback
 */
void OnEsptoolProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify);

/*
 * OnEsptoolSignal - esptool protocol signal change callback
 */
void OnEsptoolSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify);

/*
 * ResetSignalState - Reset signal state for download mode detection
 */
void ResetSignalState(void);

/*
 * OnDeviceModified - Callback when device data is modified by protocol
 */
void OnDeviceModified(void);

/*
 * UpdateMenuState - Update menu and toolbar button states
 */
void UpdateMenuState(HWND hWnd);

/*
 * UpdateTitle - Update window title bar
 */
void UpdateTitle(HWND hWnd);

/*
 * UpdateStatusBar - Update status bar display
 */
void UpdateStatusBar(void);

/*
 * CreateStatusTooltip - Create tooltip control for status bar
 */
void CreateStatusTooltip(HWND hParent);

/*
 * PromptDisconnectIfNeeded - Check if serial is connected, prompt to disconnect
 */
BOOL PromptDisconnectIfNeeded(HWND hWnd);

/*
 * PromptSaveIfNeeded - Check if device is modified, prompt to save
 */
BOOL PromptSaveIfNeeded(HWND hWnd);

/*
 * IsPortAvailable - Check if a specific port exists in the system
 */
BOOL IsPortAvailable(const WCHAR *portName);

/*
 * CanReconnect - Check if reconnect is available
 */
BOOL CanReconnect(void);

/*
 * ApplyFontToEdit - Apply font to RichEdit control
 */
void ApplyFontToEdit(HWND hEdit, LOGFONTW *plf);

/*
 * InitDefaultFont - Initialize default font settings
 */
void InitDefaultFont(void);

/*
 * Main_OnConnect - Handle Connect command
 */
void Main_OnConnect(HWND hWnd);

/*
 * Main_OnDisconnect - Handle Disconnect command
 */
void Main_OnDisconnect(HWND hWnd);

/*
 * Main_OnReconnect - Handle Reconnect command
 */
void Main_OnReconnect(HWND hWnd);

/*
 * Main_OnFlashImport - Handle Flash Import command
 */
void Main_OnFlashImport(HWND hWnd);

/*
 * Main_OnFlashExport - Handle Flash Export command
 */
void Main_OnFlashExport(HWND hWnd);

/*
 * Main_OnDumpDeviceAs - Handle Dump Device As command
 */
void Main_OnDumpDeviceAs(HWND hWnd);

/*
 * Main_OnLogClear - Handle Log Clear command
 */
void Main_OnLogClear(HWND hWnd);

/*
 * Main_OnLogFont - Handle Log Font command
 */
void Main_OnLogFont(HWND hWnd);

/*
 * Main_OnLogSaveAs - Handle Log Save As command
 */
void Main_OnLogSaveAs(HWND hWnd);

/*
 * Main_OnExit - Handle Exit command
 */
void Main_OnExit(HWND hWnd);

/*
 * Main_CmdNewDevice - Handle New Device command
 */
void Main_CmdNewDevice(HWND hWnd);

/*
 * Main_CmdOpenDevice - Handle Open Device command
 */
void Main_CmdOpenDevice(HWND hWnd);

/*
 * Main_CmdSaveDevice - Handle Save Device command
 */
void Main_CmdSaveDevice(HWND hWnd);

/*
 * Main_CmdSaveDeviceAs - Handle Save Device As command
 */
void Main_CmdSaveDeviceAs(HWND hWnd);

/*
 * Main_CmdDeviceProps - Handle Device Properties command
 */
void Main_CmdDeviceProps(HWND hWnd);

/*
 * Main_CmdKeyMgmt - Handle Key Management command
 */
void Main_CmdKeyMgmt(HWND hWnd);

/*
 * Main_CmdEncryptState - Handle encryption state menu command
 *
 * @hWnd: Main window handle
 * @state: New encryption state (0=none, 1=dev, 2=release)
 */
void Main_CmdEncryptState(HWND hWnd, int state);

/*
 * Main_CmdDownloadMode - Handle download mode menu command
 *
 * @hWnd: Main window handle
 * @mode: New download mode (0=normal, 1=secure, 2=disabled)
 */
void Main_CmdDownloadMode(HWND hWnd, int mode);

/*
 * Main_OpenDeviceFile - Open device file by path
 */
BOOL Main_OpenDeviceFile(HWND hWnd, const WCHAR *filePath);

#endif /* APP_COMMANDS_H */
