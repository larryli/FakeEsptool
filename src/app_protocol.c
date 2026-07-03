/*
 * app_protocol.c - Protocol signal state machine
 *
 * Detects DTR/RTS signal sequences to determine download mode vs normal boot.
 * Extracted from main.c to separate protocol logic from GUI coordination.
 */

#include "app_protocol.h"
#include "fesptool/chip.h"
#include "fesptool/efuse.h"
#include "fesptool/esptool.h"
#include "main.h"
#include "utils/trace.h"
#include <string.h>

extern fesp_ctx_t g_esptool;
extern fesp_chip_ctx_t g_chip;

/* Signal state for download mode detection */
static BOOL g_prev_dsr = FALSE;
static BOOL g_prev_cts = FALSE;
static BOOL g_reset_pending = FALSE;
static BOOL g_saw_io0_low = FALSE;

/* ========================================================================
 * Public API
 * ======================================================================== */

void OnEsptoolProcessData(SERIAL_CTX *ctx, const BYTE *data, DWORD len,
                          HWND hNotify)
{
    if (!ctx || !data || len == 0) {
        return;
    }
    fesp_feed(&g_esptool, data, (int)len);
}

void ResetSignalState(void)
{
    g_prev_dsr = FALSE;
    g_prev_cts = FALSE;
    g_reset_pending = FALSE;
    g_saw_io0_low = FALSE;
}

/* ========================================================================
 * Internal: boot message output
 * ======================================================================== */

static void OutputBootMessage(SERIAL_CTX *ctx, BOOL download_mode,
                              BYTE reset_cause, HWND hNotify)
{
    fesp_reset_state(&g_esptool);

    DWORD bootBaud = fesp_chip_get_boot_baud_rate(&g_chip);
    Serial_SetBaudRate(ctx, bootBaud);
    Serial_PostLogF(hNotify, L"CFG", L"Baud rate: %lu", bootBaud);

    char boot_msg_buf[512];
    const char *msg =
        fesp_chip_get_boot_message(&g_chip, (bool)download_mode, reset_cause,
                                   boot_msg_buf, sizeof(boot_msg_buf));
    if (msg[0]) {
        Serial_WriteData(ctx, (const BYTE *)msg, (DWORD)strlen(msg), hNotify);

        const char *line = msg;
        while (*line) {
            const char *end = strchr(line, '\r');
            if (!end) {
                end = line + strlen(line);
            }
            int wlen = MultiByteToWideChar(CP_UTF8, 0, line, (int)(end - line),
                                           NULL, 0);
            if (wlen > 0) {
                WCHAR *wline = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
                                                  (wlen + 1) * sizeof(WCHAR));
                if (wline) {
                    MultiByteToWideChar(CP_UTF8, 0, line, (int)(end - line),
                                        wline, wlen);
                    wline[wlen] = L'\0';
                    Serial_PostLog(hNotify, L"BOOT", wline);
                    HeapFree(GetProcessHeap(), 0, wline);
                }
            }
            line = end;
            while (*line == '\r' || *line == '\n') {
                line++;
            }
        }
    }

    /* Download mode: switch special chips (74880) to 115200 */
    if (download_mode && bootBaud != 115200) {
        Serial_SetBaudRate(ctx, 115200);
        Serial_PostLogF(hNotify, L"CFG", L"Baud rate: 115200");
    }
}

/* ========================================================================
 * Signal state machine
 *
 * Detects ClassicReset sequence (DTR/RTS) to enter download mode.
 *
 * Signal mapping (active-low logic):
 *   DTR -> GPIO0: DTR=ON -> GPIO0=LOW (select download mode)
 *   RTS -> EN:    RTS=ON -> EN=LOW (reset chip)
 *
 * ClassicReset sequence:
 *   Step 1: DTR=OFF, RTS=ON  -> DSR=OFF, CTS=ON  (EN=LOW, GPIO0=HIGH)
 *   Step 2: DTR=ON,  RTS=OFF -> DSR=ON,  CTS=OFF (GPIO0=LOW, EN=HIGH)
 *   Step 3: DTR=OFF, RTS=OFF -> DSR=OFF, CTS=OFF (release, enter mode)
 *
 * State transition table:
 *   DSR  CTS  g_reset_pending  g_saw_io0_low  Action
 *   OFF  ON   FALSE            -              Start reset, set pending
 *   ON   OFF  TRUE             FALSE          Set IO0 low flag
 *   ON   ON   TRUE             -              Intermediate (ignore)
 *   OFF  OFF  TRUE             TRUE           ClassicReset: download mode
 *   OFF  OFF  TRUE             FALSE          HardReset: normal boot
 *   Other combinations                      Cancel pending reset
 * ======================================================================== */

void OnEsptoolSignal(SERIAL_CTX *ctx, DWORD modemStatus, HWND hNotify)
{
    BOOL dsr = (modemStatus & MS_DSR_ON) != 0;
    BOOL cts = (modemStatus & MS_CTS_ON) != 0;

    if (dsr != g_prev_dsr || cts != g_prev_cts) {
        Serial_PostLogF(hNotify, L"SIG", L"DSR:%s CTS:%s", dsr ? L"ON" : L"OFF",
                        cts ? L"ON" : L"OFF");

        if (!dsr && cts) {
            g_reset_pending = TRUE;
            g_saw_io0_low = FALSE;
        } else if (g_reset_pending && dsr && !cts) {
            g_saw_io0_low = TRUE;
        } else if (g_reset_pending && dsr && cts) {
            /* Intermediate state: keep pending */
        } else if (g_reset_pending && !dsr && !cts) {
            if (g_saw_io0_low) {
                Serial_PostLog(hNotify, L"SIG", L"Download mode entered");
                fesp_efuse_clear_volatile(&g_chip);
                OutputBootMessage(ctx, TRUE, 0x01, hNotify);
            } else {
                Serial_PostLog(hNotify, L"SIG", L"Hard reset (normal boot)");
                OutputBootMessage(ctx, FALSE, 0x02, hNotify);
            }
            g_reset_pending = FALSE;
            g_saw_io0_low = FALSE;
        } else {
            g_reset_pending = FALSE;
            g_saw_io0_low = FALSE;
        }

        g_prev_dsr = dsr;
        g_prev_cts = cts;
    }
}
