/*
 * about.c - About dialog
 *
 * Displays application version info and copyright.
 */

#include "../resource.h"
#include "../utils/lang.h"
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <windows.h>
#include <winver.h>

/*
 * AboutDlgProc - About dialog procedure
 */
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        /* Read version info from exe */
        TCHAR szPath[MAX_PATH];
        GetModuleFileName(NULL, szPath, MAX_PATH);
        DWORD dwInfo;
        UINT size = GetFileVersionInfoSize(szPath, &dwInfo);
        if (size > 0) {
            void *pInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
            if (pInfo && GetFileVersionInfo(szPath, dwInfo, size, pInfo)) {
                void *ptr;
                if (VerQueryValue(pInfo, L"\\VarFileInfo\\Translation", &ptr,
                                  &size)) {
                    WORD *pLang = (WORD *)ptr;
                    TCHAR szBuf[MAX_PATH];

                    /* Product name + version */
                    swprintf(szBuf, MAX_PATH,
                             L"\\StringFileInfo\\%04X%04X\\%ls", *pLang,
                             *(pLang + 1), L"ProductName");
                    if (VerQueryValue(pInfo, szBuf, &ptr, &size)) {
                        void *ptr2;
                        swprintf(szBuf, MAX_PATH,
                                 L"\\StringFileInfo\\%04X%04X\\%ls", *pLang,
                                 *(pLang + 1), L"ProductVersion");
                        if (VerQueryValue(pInfo, szBuf, &ptr2, &size)) {
                            swprintf(szBuf, MAX_PATH, L"%ls v%ls", (LPTSTR)ptr,
                                     (LPTSTR)ptr2);
                            SetDlgItemText(hDlg, IDD_APPNAME, szBuf);
                        }
                    }

                    /* Copyright */
                    swprintf(szBuf, MAX_PATH,
                             L"\\StringFileInfo\\%04X%04X\\%ls", *pLang,
                             *(pLang + 1), L"LegalCopyright");
                    if (VerQueryValue(pInfo, szBuf, &ptr, &size)) {
                        SetDlgItemText(hDlg, IDD_COPYRIGHT, (LPTSTR)ptr);
                    }
                }
            }
            if (pInfo) {
                HeapFree(GetProcessHeap(), 0, pInfo);
            }
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
            ShellExecute(NULL, L"open", ((PNMLINK)lParam)->item.szUrl, NULL,
                         NULL, SW_SHOW);
        break;
    }
    return FALSE;
}
