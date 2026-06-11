/*
 * port_select.c - Serial port selection dialog
 *
 * Enumerates available serial ports and allows user to select one.
 */

#include "dlg.h"
#include "../serial.h"
#include "../utils/config.h"
#include "../utils/lang.h"
#include <string.h>

/*
 * PortSelectDlgProc - Port selection dialog procedure
 */
INT_PTR CALLBACK PortSelectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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

/*
 * ShowPortSelectDialog - Show port selection dialog
 */
BOOL ShowPortSelectDialog(HWND hWnd)
{
    INT_PTR ret = DialogBoxW(GetModuleHandle(NULL),
                             MAKEINTRESOURCEW(IDD_PORT_SELECT), hWnd, PortSelectDlgProc);
    if (ret == IDOK) {
        return TRUE;
    }
    return FALSE;
}
