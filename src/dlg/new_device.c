/*
 * new_device.c - New Device dialog
 *
 * Handles creation of new ESP device with chip type, flash size,
 * MAC address, and optional initial flash file.
 */

#include "dlg.h"
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

/* New Device dialog procedure */
INT_PTR CALLBACK NewDeviceDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
            SendMessageW(hChip, CB_SETCURSEL, 0, 0);

            HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
            PopulateFlashSizes(hFlash, CHIP_ESP8266, 4*1024*1024);

            HWND hXtal = GetDlgItem(hDlg, IDC_XTAL_FREQ_COMBO);
            SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"40MHz");
            SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"26MHz");
            SendMessageW(hXtal, CB_SETCURSEL, 0, 0);

            CheckDlgButton(hDlg, IDC_INIT_BLANK, BST_CHECKED);
            EnableWindow(GetDlgItem(hDlg, IDC_INIT_FILE_PATH), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BROWSE_FILE), FALSE);

            WCHAR macStr[32];
            wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            SetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr);
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHIP_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HWND hChip = GetDlgItem(hDlg, IDC_CHIP_COMBO);
                int chipSel = (int)SendMessageW(hChip, CB_GETCURSEL, 0, 0);
                HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
                PopulateFlashSizes(hFlash, (CHIP_TYPE)chipSel, 4*1024*1024);
                /* Enable/disable XTAL freq combo based on chip type */
                BOOL xtalEditable = (chipSel == CHIP_ESP8266 ||
                                     chipSel == CHIP_ESP32 ||
                                     chipSel == CHIP_ESP32C2);
                EnableWindow(GetDlgItem(hDlg, IDC_XTAL_FREQ_COMBO), xtalEditable);
            }
            return TRUE;

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
                selectedFlash = GetFlashSizeFromCombo(hFlash, selectedChip);

                WCHAR macStr[32];
                GetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr, 32);
                unsigned int tmp[6] = {0};
                swscanf(macStr, L"%x:%x:%x:%x:%x:%x",
                        &tmp[0], &tmp[1], &tmp[2], &tmp[3], &tmp[4], &tmp[5]);
                for (int j = 0; j < 6; j++)
                    mac[j] = (BYTE)tmp[j];

                HWND hXtal = GetDlgItem(hDlg, IDC_XTAL_FREQ_COMBO);
                BYTE xtalFreq = (BYTE)SendMessageW(hXtal, CB_GETCURSEL, 0, 0);

                Device_Close(&g_device);
                if (Device_Init(&g_device, selectedChip, selectedFlash, mac)) {
                    g_device.chip.xtal_freq = xtalFreq;

                    /* Load initial flash from file if selected */
                    if (IsDlgButtonChecked(hDlg, IDC_INIT_FILE) == BST_CHECKED) {
                        WCHAR filePath[MAX_PATH] = {0};
                        GetDlgItemTextW(hDlg, IDC_INIT_FILE_PATH, filePath, MAX_PATH);
                        if (filePath[0]) {
                            /* Show busy cursor and disable dialog */
                            HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
                            EnableWindow(hDlg, FALSE);

                            HANDLE hFile = CreateFileW(filePath, GENERIC_READ, 0, NULL,
                                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (hFile != INVALID_HANDLE_VALUE) {
                                DWORD fileSize = GetFileSize(hFile, NULL);
                                if (fileSize <= selectedFlash) {
                                    DWORD bytesRead;
                                    ReadFile(hFile, g_device.flash.data, fileSize, &bytesRead, NULL);
                                }
                                CloseHandle(hFile);
                            }

                            /* Restore dialog state */
                            EnableWindow(hDlg, TRUE);
                            SetCursor(hOldCursor);
                        }
                    }
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
