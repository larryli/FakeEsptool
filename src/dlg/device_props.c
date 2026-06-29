/*
 * device_props.c - Device Properties dialog
 *
 * Allows modifying chip type, flash size, MAC address, and crystal frequency
 * of an existing device.
 */

#include "dlg.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/*
 * PopulateXtalFreqs - Fill XTAL frequency combo for given chip type
 *
 * Adds only valid frequency options, selects the current value,
 * and enables/disables the combo appropriately.
 */
static void PopulateXtalFreqs(HWND hXtal, fesp_chip_type_t chip, BYTE curXtal)
{
    SendMessageW(hXtal, CB_RESETCONTENT, 0, 0);

    switch (chip) {
    case FESP_CHIP_ESP8266:
    case FESP_CHIP_ESP32:
    case FESP_CHIP_ESP32C2:
        /* 26MHz, 40MHz selectable */
        SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"40MHz");
        SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"26MHz");
        EnableWindow(hXtal, TRUE);
        SendMessageW(hXtal, CB_SETCURSEL, curXtal, 0);
        break;
    case FESP_CHIP_ESP32C5:
        /* 40MHz, 48MHz selectable */
        SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"40MHz");
        SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"48MHz");
        EnableWindow(hXtal, TRUE);
        /* Map: 40MHz=index0, 48MHz=index1; curXtal 0->0, 2->1 */
        SendMessageW(hXtal, CB_SETCURSEL, curXtal == FESP_XTAL_FREQ_48M ? 1 : 0, 0);
        break;
    case FESP_CHIP_ESP32H2:
        /* Fixed 32MHz */
        SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"32MHz");
        EnableWindow(hXtal, FALSE);
        SendMessageW(hXtal, CB_SETCURSEL, 0, 0);
        break;
    default:
        /* Fixed 40MHz: S2, S3, C3, C6, C61, P4 */
        SendMessageW(hXtal, CB_ADDSTRING, 0, (LPARAM)L"40MHz");
        EnableWindow(hXtal, FALSE);
        SendMessageW(hXtal, CB_SETCURSEL, 0, 0);
        break;
    }
}

/*
 * DevicePropsDlgProc - Device Properties dialog procedure
 */
INT_PTR CALLBACK DevicePropsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam)
{
    static BYTE mac[6];
    static fesp_chip_type_t selectedChip;
    static DWORD selectedFlash;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG: {
        /* Save current values */
        selectedChip = g_chip.type;
        selectedFlash = g_flash.size;
        memcpy(mac, g_chip.mac, 6);

        HWND hChip = GetDlgItem(hDlg, IDC_CHIP_COMBO);
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP8266");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-S2");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-S3");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C2");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C3");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C6");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C5");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-C61");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-H2");
        SendMessageW(hChip, CB_ADDSTRING, 0, (LPARAM)L"ESP32-P4");
        SendMessageW(hChip, CB_SETCURSEL, (WPARAM)selectedChip, 0);

        HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
        PopulateFlashSizes(hFlash, selectedChip, selectedFlash);

        HWND hXtal = GetDlgItem(hDlg, IDC_XTAL_FREQ_COMBO);
        PopulateXtalFreqs(hXtal, selectedChip, g_chip.xtal_freq);

        WCHAR macStr[32];
        wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
                  mac[2], mac[3], mac[4], mac[5]);
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
                PopulateFlashSizes(hFlash, (fesp_chip_type_t)chipSel,
                                   g_flash.size);
                /* Repopulate XTAL freq combo for new chip type */
                HWND hXtal = GetDlgItem(hDlg, IDC_XTAL_FREQ_COMBO);
                PopulateXtalFreqs(hXtal, (fesp_chip_type_t)chipSel,
                                  FESP_XTAL_FREQ_40M);
            }
            return TRUE;

        case IDC_RANDOM_MAC: {
            mac[0] = 0xAA;
            mac[1] = 0xBB;
            mac[2] = (BYTE)(rand() & 0xFF);
            mac[3] = (BYTE)(rand() & 0xFF);
            mac[4] = (BYTE)(rand() & 0xFF);
            mac[5] = (BYTE)(rand() & 0xFF);
            WCHAR macStr[32];
            wsprintfW(macStr, L"%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
                      mac[2], mac[3], mac[4], mac[5]);
            SetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr);
        }
            return TRUE;

        case IDOK: {
            HWND hChip = GetDlgItem(hDlg, IDC_CHIP_COMBO);
            int chipSel = (int)SendMessageW(hChip, CB_GETCURSEL, 0, 0);
            selectedChip = (fesp_chip_type_t)chipSel;

            HWND hFlash = GetDlgItem(hDlg, IDC_FLASH_SIZE_COMBO);
            selectedFlash = GetFlashSizeFromCombo(hFlash, selectedChip);

            WCHAR macStr[32];
            GetDlgItemTextW(hDlg, IDC_MAC_EDIT, macStr, 32);
            unsigned int tmp[6] = {0};
            swscanf(macStr, L"%x:%x:%x:%x:%x:%x", &tmp[0], &tmp[1], &tmp[2],
                    &tmp[3], &tmp[4], &tmp[5]);
            for (int j = 0; j < 6; j++) {
                mac[j] = (BYTE)tmp[j];
            }

            HWND hXtal = GetDlgItem(hDlg, IDC_XTAL_FREQ_COMBO);
            int xtalIdx = (int)SendMessageW(hXtal, CB_GETCURSEL, 0, 0);
            BYTE xtalFreq;
            switch (selectedChip) {
            case FESP_CHIP_ESP8266:
            case FESP_CHIP_ESP32:
            case FESP_CHIP_ESP32C2:
                /* index 0=40MHz, 1=26MHz */
                xtalFreq = (xtalIdx == 1) ? FESP_XTAL_FREQ_26M : FESP_XTAL_FREQ_40M;
                break;
            case FESP_CHIP_ESP32C5:
                /* index 0=40MHz, 1=48MHz */
                xtalFreq = (xtalIdx == 1) ? FESP_XTAL_FREQ_48M : FESP_XTAL_FREQ_40M;
                break;
            case FESP_CHIP_ESP32H2:
                xtalFreq = FESP_XTAL_FREQ_32M;
                break;
            default:
                xtalFreq = FESP_XTAL_FREQ_40M;
                break;
            }

            /* Check if anything changed */
            BOOL changed = (selectedChip != g_chip.type) ||
                           (selectedFlash != g_flash.size) ||
                           (memcmp(mac, g_chip.mac, 6) != 0) ||
                           (xtalFreq != g_chip.xtal_freq);

            if (changed) {
                /* Reinitialize device with new settings */
                DWORD oldFlashSize = g_flash.size;
                BYTE *oldFlashData = g_flash.data;
                g_flash.data = NULL; /* Prevent double-free */

                fesp_flash_close(&g_flash);
                fesp_chip_close(&g_chip);
                if (fesp_chip_init(&g_chip, selectedChip) &&
                    fesp_flash_init(&g_flash, selectedFlash)) {
                    fesp_chip_set_flash_size(&g_chip, selectedFlash);
                    fesp_chip_set_mac(&g_chip, mac);
                    g_chip.xtal_freq = xtalFreq;

                    /* Copy old flash data if same size or larger */
                    if (oldFlashData && oldFlashSize <= selectedFlash) {
                        memcpy(g_flash.data, oldFlashData, oldFlashSize);
                    }
                    if (oldFlashData) {
                        HeapFree(GetProcessHeap(), 0, oldFlashData);
                    }

                    g_deviceModified = TRUE;
                    EndDialog(hDlg, IDOK);
                } else {
                    if (oldFlashData) {
                        HeapFree(GetProcessHeap(), 0, oldFlashData);
                    }
                    MessageBoxW(hDlg, LoadStr(IDS_MSG_FAIL_UPDATE_DEV),
                                LoadStr(IDS_MSG_ERROR), MB_OK | MB_ICONERROR);
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
