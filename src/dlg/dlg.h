/*
 * dlg.h - Shared declarations for dialog modules
 */

#ifndef DLG_H
#define DLG_H

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include "../resource.h"
#include "../esptool/chip.h"
#include "../esptool/device.h"
#include "../esptool/esptool.h"

/* Global state (defined in main.c) */
extern DEVICE_CTX g_device;
extern ESPTOOL_CTX g_esptool;
extern WCHAR g_szSelectedPort[32];

/* Helper functions (defined in main.c) */
void PopulateFlashSizes(HWND hFlash, CHIP_TYPE chip, DWORD currentSize);
DWORD GetFlashSizeFromCombo(HWND hFlash, CHIP_TYPE chip);

/* Callback when device data is modified (defined in main.c) */
void OnDeviceModified(void);

/* Dialog procedures (defined in respective .c files) */
INT_PTR CALLBACK DevicePropsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK PortSelectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* Port selection dialog (defined in port_select.c) */
BOOL ShowPortSelectDialog(HWND hWnd);

#endif /* DLG_H */
