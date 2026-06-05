/*
 * device_props.c - Device Properties dialog
 *
 * Allows modifying chip type, flash size, MAC address, and crystal frequency
 * of an existing device.
 */

#include "dlg.h"
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

/* External: SyncDeviceToEsptool defined in main.c */
extern void SyncDeviceToEsptool(void);

/* Device Properties dialog procedure */
INT_PTR CALLBACK DevicePropsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
            SendMessageW(hChip, CB_SETCURSEL, (WPARAM)selectedChip, 0);

            HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
            PopulateFlashSizes(hFlash, selectedChip, selectedFlash);

            HWND hXtal = GetDlgItem(hDlg, IDC_XTAL_FREQ_COMBO);
            SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"40MHz");
            SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"26MHz");
            SendMessageW(hXtal, CB_SETCURSEL, g_device.chip.xtal_freq, 0);
            /* Disable XTAL freq for fixed-xtal chips */
            if (selectedChip == CHIP_ESP32C3 || selectedChip == CHIP_ESP32C6 ||
                selectedChip == CHIP_ESP32S2 || selectedChip == CHIP_ESP32S3) {
                EnableWindow(hXtal, FALSE);
            }

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
                PopulateFlashSizes(hFlash, (CHIP_TYPE)chipSel, g_device.flash.size);
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

                /* Check if anything changed */
                BOOL changed = (selectedChip != g_device.chip.type) ||
                               (selectedFlash != g_device.flash.size) ||
                               (memcmp(mac, g_device.chip.mac, 6) != 0) ||
                               (xtalFreq != g_device.chip.xtal_freq);

                if (changed) {
                    /* Reinitialize device with new settings */
                    DWORD oldFlashSize = g_device.flash.size;
                    BYTE *oldFlashData = g_device.flash.data;
                    g_device.flash.data = NULL;
                    g_device.flash.allocated = FALSE;

                    Device_Close(&g_device);
                    if (Device_Init(&g_device, selectedChip, selectedFlash, mac)) {
                        g_device.chip.xtal_freq = xtalFreq;

                        /* Copy old flash data if same size or larger */
                        if (oldFlashData && oldFlashSize <= selectedFlash) {
                            memcpy(g_device.flash.data, oldFlashData, oldFlashSize);
                        }
                        if (oldFlashData)
                            HeapFree(GetProcessHeap(), 0, oldFlashData);

                        SyncDeviceToEsptool();
                        Esptool_SetModifiedCallback(&g_esptool, OnDeviceModified);
                        Device_SetModified(&g_device, TRUE);
                        EndDialog(hDlg, IDOK);
                    } else {
                        if (oldFlashData)
                            HeapFree(GetProcessHeap(), 0, oldFlashData);
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
