/*
 * dlg.h - Shared declarations for dialog modules
 */

#ifndef DLG_H
#define DLG_H

#include "../esptool/chip.h"
#include "../esptool/flash.h"
#include "../esptool/esptool.h"
#include "../resource.h"
#include "../utils/lang.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>

/* Global state (defined in main.c) */
extern CHIP_CTX g_chip;
extern FLASH_CTX g_flash;
extern WCHAR g_deviceFile[MAX_PATH];
extern BOOL g_deviceModified;
extern ESPTOOL_CTX g_esptool;
extern WCHAR g_szSelectedPort[32];

/*
 * PopulateFlashSizes - Populate flash size combo box
 */
void PopulateFlashSizes(HWND hFlash, CHIP_TYPE chip, DWORD currentSize);

/*
 * GetFlashSizeFromCombo - Get flash size from combo box selection
 */
DWORD GetFlashSizeFromCombo(HWND hFlash, CHIP_TYPE chip);

/*
 * OnDeviceModified - Callback when device data is modified by protocol
 */
void OnDeviceModified(void);

/*
 * DevicePropsDlgProc - Device Properties dialog procedure
 */
INT_PTR CALLBACK DevicePropsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam);

/*
 * PortSelectDlgProc - Port selection dialog procedure
 */
INT_PTR CALLBACK PortSelectDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                   LPARAM lParam);

/*
 * AboutDlgProc - About dialog procedure
 */
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                              LPARAM lParam);

/*
 * KeyMgmtDlgProc - Key Management dialog procedure
 */
INT_PTR CALLBACK KeyMgmtDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                LPARAM lParam);

/*
 * ShowPortSelectDialog - Show port selection dialog
 */
BOOL ShowPortSelectDialog(HWND hWnd);

#endif /* DLG_H */
