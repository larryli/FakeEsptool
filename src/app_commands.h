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
#define STATUS_PART1_WIDTH  100  /* Chip model */
#define STATUS_PART2_WIDTH  80   /* Flash size */
#define STATUS_PART3_WIDTH  150  /* MAC address */
#define STATUS_PART4_WIDTH  120  /* Port name */
#define STATUS_PART5_WIDTH  140  /* Port config */

/* Message size limit */
#define MAX_MSG_SIZE        65536

/* Serial callbacks (defined in main.c) */
void OnEsptoolProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len, HWND hNotify);
void OnEsptoolSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify);
void ResetSignalState(void);

/* Device sync (defined in main.c) */
void SyncDeviceToEsptool(void);
void OnDeviceModified(void);

/* Helper functions (defined in app_commands.c, used by main.c) */
void UpdateMenuState(HWND hWnd);
void UpdateTitle(HWND hWnd);
void UpdateStatusBar(void);
BOOL PromptDisconnectIfNeeded(HWND hWnd);
BOOL PromptSaveIfNeeded(HWND hWnd);
BOOL IsPortAvailable(const WCHAR *portName);
BOOL CanReconnect(void);

/* Font management (defined in app_commands.c) */
void ApplyFontToEdit(HWND hEdit, LOGFONTW *plf);
void InitDefaultFont(void);

/* Command handlers (defined in app_commands.c) */
void Main_OnConnect(HWND hWnd);
void Main_OnDisconnect(HWND hWnd);
void Main_OnReconnect(HWND hWnd);
void Main_OnFlashImport(HWND hWnd);
void Main_OnFlashExport(HWND hWnd);
void Main_OnDumpDeviceAs(HWND hWnd);
void Main_OnLogClear(HWND hWnd);
void Main_OnLogFont(HWND hWnd);
void Main_OnLogSaveAs(HWND hWnd);
void Main_OnExit(HWND hWnd);
void Main_CmdNewDevice(HWND hWnd);
void Main_CmdOpenDevice(HWND hWnd);
void Main_CmdSaveDevice(HWND hWnd);
void Main_CmdSaveDeviceAs(HWND hWnd);
void Main_CmdDeviceProps(HWND hWnd);

#endif /* APP_COMMANDS_H */
