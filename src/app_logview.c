/*
 * app_logview.c - Log display functions
 *
 * Provides colored log display in RichEdit control.
 * Supports three types of log entries:
 * - RX/TX data: Hex dump with timestamp and direction
 * - Custom text: Protocol messages with tag
 * - Signal/Config: System events with colored tags
 */

#include "app_logview.h"
#include "serial.h"
#include <richedit.h>
#include <stdio.h>

/*
 * SetEditColor - Set text color for current selection
 *
 * @hEdit: Handle to RichEdit control
 * @color: RGB color value
 */
static void SetEditColor(HWND hEdit, COLORREF color)
{
    CHARFORMAT2W cf = {0};
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

/*
 * AppendColoredText - Append colored text to RichEdit control
 *
 * Moves caret to end, sets color, and appends text.
 *
 * @hEdit: Handle to RichEdit control
 * @text:  Text to append
 * @len:   Text length in characters
 * @color: RGB color value
 */
static void AppendColoredText(HWND hEdit, const WCHAR *text, int len, COLORREF color)
{
    int textLen = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, textLen, textLen);
    SetEditColor(hEdit, color);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

/*
 * Main_AppendLog - Format and append RX/TX data to log display
 *
 * Formats binary data as hex dump with timestamp and direction indicator.
 * Display format:
 *   2026-06-07 14:30:25.123 [RX] C0 00 08 00 ...
 *   2026-06-07 14:30:25.124 [TX] C0 01 08 00 ...
 *
 * Hex grouping: 8 bytes per group, 16 bytes per line
 *
 * @hMainWnd: Main window handle (unused)
 * @data:     Pointer to data bytes
 * @len:      Number of bytes
 * @dir:      Direction (DIR_RX or DIR_TX)
 */
void Main_AppendLog(HWND hMainWnd, const BYTE *data, DWORD len, int dir)
{
    (void)hMainWnd;
    if (!g_hEdit || len == 0)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Build timestamp: "YYYY-MM-DD HH:MM:SS.mmm " */
    WCHAR timestamp[32];
    int tsLen = wsprintfW(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                          st.wYear, st.wMonth, st.wDay,
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    /* Build direction: "[RX] " or "[TX] " */
    WCHAR direction[8];
    int dirLen = wsprintfW(direction, L"[%s] ", (dir == DIR_RX) ? L"RX" : L"TX");
    COLORREF dirColor = (dir == DIR_RX) ? COLOR_RX : COLOR_TX;

    /* Calculate max line size for HEX data */
    DWORD numLines = (len + 15) / 16;
    DWORD maxLineSize = (tsLen + dirLen + 50) * (numLines + 1) + (len * 4) + 64;
    WCHAR *hexLine = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, maxLineSize * sizeof(WCHAR));
    if (!hexLine)
        return;

    int pos = 0;
    int prefixLen = tsLen + dirLen;

    /* Format HEX data with grouping and line wrapping */
    for (DWORD i = 0; i < len; i++) {
        if (i > 0 && i % 16 == 0) {
            /* New line, align with prefix */
            hexLine[pos++] = L'\r';
            hexLine[pos++] = L'\n';
            for (int j = 0; j < prefixLen; j++)
                hexLine[pos++] = L' ';
        } else if (i > 0 && i % 8 == 0) {
            /* Extra space every 8 bytes */
            hexLine[pos++] = L' ';
        }
        pos += wsprintfW(hexLine + pos, L"%02X ", data[i]);
    }
    hexLine[pos++] = L'\r';
    hexLine[pos++] = L'\n';
    hexLine[pos] = L'\0';

    /* Append with colors: timestamp (gray) + direction (color) + data (black) */
    AppendColoredText(g_hEdit, timestamp, tsLen, COLOR_TIMESTAMP);
    AppendColoredText(g_hEdit, direction, dirLen, dirColor);
    AppendColoredText(g_hEdit, hexLine, pos, COLOR_DATA);

    /* Scroll to end */
    int textLen = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);

    HeapFree(GetProcessHeap(), 0, hexLine);
}

/*
 * Main_AppendCustomLog - Format and append custom text to log display
 *
 * Displays protocol messages with tag in orange color.
 * Display format:
 *   2026-06-07 14:30:25.123 [ESP] Sync handshake
 *
 * @hMainWnd: Main window handle (unused)
 * @tag:      Tag text (e.g. "ESP", "SER")
 * @text:     Message text
 */
void Main_AppendCustomLog(HWND hMainWnd, const WCHAR *tag, const WCHAR *text)
{
    (void)hMainWnd;
    if (!g_hEdit || !tag || !text)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Build timestamp: "YYYY-MM-DD HH:MM:SS.mmm " */
    WCHAR timestamp[32];
    int tsLen = wsprintfW(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                          st.wYear, st.wMonth, st.wDay,
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    /* Build tag: "[tag] " */
    WCHAR tagStr[64];
    int tagLen = wsprintfW(tagStr, L"[%s] ", tag);

    /* Append with colors: timestamp (gray) + tag (orange) + text (black) */
    AppendColoredText(g_hEdit, timestamp, tsLen, COLOR_TIMESTAMP);
    AppendColoredText(g_hEdit, tagStr, tagLen, COLOR_CUSTOM);
    AppendColoredText(g_hEdit, text, lstrlenW(text), COLOR_DATA);

    /* Append newline */
    AppendColoredText(g_hEdit, L"\r\n", 2, COLOR_DATA);

    /* Scroll to end */
    int textLen = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
}

/*
 * Main_AppendSignalLog - Format and append signal/config log
 *
 * Displays system events (signal changes, config updates) with colored tag.
 * Display format:
 *   2026-06-07 14:30:25.123 [SIG] DSR:ON CTS:OFF
 *   2026-06-07 14:30:25.124 [CFG] 115200,8N1
 *
 * @tag:      Tag text (e.g. "SIG", "CFG")
 * @text:     Event text
 * @tagColor: RGB color for tag
 */
void Main_AppendSignalLog(const WCHAR *tag, const WCHAR *text, COLORREF tagColor)
{
    if (!g_hEdit || !tag || !text)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Build timestamp: "YYYY-MM-DD HH:MM:SS.mmm " */
    WCHAR timestamp[32];
    int tsLen = wsprintfW(timestamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                          st.wYear, st.wMonth, st.wDay,
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    /* Build tag: "[tag] " */
    WCHAR tagStr[64];
    int tagLen = wsprintfW(tagStr, L"[%s] ", tag);

    /* Append with colors: timestamp (gray) + tag (signal/config color) + text (gray) */
    AppendColoredText(g_hEdit, timestamp, tsLen, COLOR_TIMESTAMP);
    AppendColoredText(g_hEdit, tagStr, tagLen, tagColor);
    AppendColoredText(g_hEdit, text, lstrlenW(text), COLOR_TIMESTAMP);

    /* Append newline */
    AppendColoredText(g_hEdit, L"\r\n", 2, COLOR_DATA);

    /* Scroll to end */
    int textLen = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
}
