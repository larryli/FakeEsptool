/*
 * app_logview.c - Log display functions
 *
 * Provides buffered, batch-updated colored log display in RichEdit control.
 *
 * Performance optimization:
 * - Log entries are buffered in a linked list
 * - Timer (50ms) triggers batch flush to RichEdit
 * - WM_SETREDRAW is used to disable repaint during batch update
 * - This prevents UI freezing during heavy logging
 */

#include "app_logview.h"
#include "serial.h"
#include <richedit.h>
#include <stdio.h>

/* ============================================================================
 * Log entry buffer
 * ============================================================================ */

/* Log entry types */
typedef enum {
    LOG_TYPE_DATA,      /* RX/TX hex data */
    LOG_TYPE_CUSTOM,    /* Custom text with tag */
    LOG_TYPE_SIGNAL     /* Signal/config with colored tag */
} LOG_ENTRY_TYPE;

/* Log entry structure */
typedef struct LOG_ENTRY {
    struct LOG_ENTRY *next;     /* Next in linked list */
    LOG_ENTRY_TYPE type;        /* Entry type */
    COLORREF color1;            /* Primary color (direction or tag) */
    COLORREF color2;            /* Secondary color (data or text) */
    WCHAR *text;                /* Formatted text (dynamically allocated) */
    int textLen;                /* Text length in characters */
} LOG_ENTRY;

/* Buffer state */
static LOG_ENTRY *g_logHead = NULL;     /* Head of pending entries list */
static LOG_ENTRY *g_logTail = NULL;     /* Tail of pending entries list */
static int g_logCount = 0;              /* Number of pending entries */
static CRITICAL_SECTION g_logLock;      /* Thread safety */
static HWND g_hMainWnd = NULL;          /* Main window handle */
static BOOL g_initialized = FALSE;

/* ============================================================================
 * Buffer management
 * ============================================================================ */

/*
 * AddEntry - Add a log entry to the buffer
 *
 * Thread-safe. Entry is appended to the tail of the list.
 */
static void AddEntry(LOG_ENTRY *entry)
{
    EnterCriticalSection(&g_logLock);
    
    entry->next = NULL;
    if (g_logTail) {
        g_logTail->next = entry;
    } else {
        g_logHead = entry;
    }
    g_logTail = entry;
    g_logCount++;
    
    LeaveCriticalSection(&g_logLock);
}

/*
 * RemoveAllEntries - Remove and free all pending entries
 *
 * Returns pointer to first entry (caller must free chain).
 */
static LOG_ENTRY *RemoveAllEntries(void)
{
    EnterCriticalSection(&g_logLock);
    
    LOG_ENTRY *head = g_logHead;
    g_logHead = NULL;
    g_logTail = NULL;
    g_logCount = 0;
    
    LeaveCriticalSection(&g_logLock);
    return head;
}

/*
 * FreeEntryChain - Free a chain of log entries
 */
static void FreeEntryChain(LOG_ENTRY *head)
{
    while (head) {
        LOG_ENTRY *next = head->next;
        if (head->text)
            HeapFree(GetProcessHeap(), 0, head->text);
        HeapFree(GetProcessHeap(), 0, head);
        head = next;
    }
}

/* High-precision timing for relative timestamps */
static LARGE_INTEGER g_freq = {0};        /* Performance counter frequency */
static LARGE_INTEGER g_lastCounter = {0}; /* Last performance counter value */

/*
 * FormatTimestamp - Format current time as timestamp string with relative delta
 *
 * Format: "YYYY-MM-DD HH:MM:SS.mmm +X.XXX "
 *         Absolute time + relative delta since last log entry (millisecond precision)
 */
static int FormatTimestamp(WCHAR *buf, int maxLen)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Get high-precision counter */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    /* Calculate relative time delta in milliseconds */
    DWORD deltaMs = 0;
    if (g_freq.QuadPart != 0 && g_lastCounter.QuadPart != 0) {
        LONGLONG deltaTicks = now.QuadPart - g_lastCounter.QuadPart;
        deltaMs = (DWORD)(deltaTicks * 1000 / g_freq.QuadPart);
    }
    g_lastCounter = now;

    return wsprintfW(buf, L"%04d-%02d-%02d %02d:%02d:%02d.%03d +%lu.%03lu ",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                     deltaMs / 1000, deltaMs % 1000);
}

/* ============================================================================
 * RichEdit update helpers
 * ============================================================================ */

/*
 * SetEditColor - Set text color for current selection
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
 */
static void AppendColoredText(HWND hEdit, const WCHAR *text, int len, COLORREF color)
{
    int textLen = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, textLen, textLen);
    SetEditColor(hEdit, color);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/*
 * LogView_Init - Initialize log view subsystem
 */
void LogView_Init(HWND hWnd)
{
    if (g_initialized)
        return;
    
    InitializeCriticalSection(&g_logLock);
    g_hMainWnd = hWnd;
    g_logHead = NULL;
    g_logTail = NULL;
    g_logCount = 0;
    
    /* Initialize high-precision timing */
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_lastCounter);
    
    g_initialized = TRUE;
    
    /* Start flush timer */
    SetTimer(hWnd, LOG_FLUSH_TIMER_ID, LOG_FLUSH_INTERVAL_MS, NULL);
}

/*
 * LogView_Close - Shutdown log view subsystem
 */
void LogView_Close(void)
{
    if (!g_initialized)
        return;
    
    /* Stop timer */
    if (g_hMainWnd)
        KillTimer(g_hMainWnd, LOG_FLUSH_TIMER_ID);
    
    /* Flush remaining entries */
    LogView_Flush();
    
    /* Free lock */
    DeleteCriticalSection(&g_logLock);
    g_initialized = FALSE;
}

/*
 * LogView_FlushTimer - Timer callback for batch log flush
 */
void LogView_FlushTimer(void)
{
    if (!g_hEdit || g_logCount == 0)
        return;
    
    /* Remove all pending entries */
    LOG_ENTRY *head = RemoveAllEntries();
    if (!head)
        return;
    
    /* Disable redraw for batch update */
    SendMessageW(g_hEdit, WM_SETREDRAW, FALSE, 0);
    
    /* Append all entries */
    LOG_ENTRY *entry = head;
    while (entry) {
        if (entry->text && entry->textLen > 0) {
            AppendColoredText(g_hEdit, entry->text, entry->textLen, entry->color1);
        }
        entry = entry->next;
    }
    
    /* Re-enable redraw and invalidate */
    SendMessageW(g_hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hEdit, NULL, TRUE);
    
    /* Scroll to end */
    int textLen = GetWindowTextLengthW(g_hEdit);
    SendMessageW(g_hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(g_hEdit, EM_SCROLLCARET, 0, 0);
    
    /* Free entries */
    FreeEntryChain(head);
}

/*
 * LogView_Flush - Flush all buffered log entries immediately
 */
void LogView_Flush(void)
{
    LogView_FlushTimer();
}

/*
 * Main_AppendLog - Format and append RX/TX data to log buffer
 *
 * Format: timestamp [RX/TX] HH HH HH ... HH HH HH ... |ASCII....|
 *         Each line shows 16 bytes with ASCII decode at end.
 */
void Main_AppendLog(HWND hMainWnd, const BYTE *data, DWORD len, int dir)
{
    (void)hMainWnd;
    if (!g_initialized || len == 0)
        return;

    /* Calculate buffer size:
     * - Timestamp: 24 chars "YYYY-MM-DD HH:MM:SS.mmm "
     * - Direction: 5 chars "[RX] " or "[TX] "
     * - Per line (hex): 16*3 (hex bytes) + 1 (extra space at byte 8) = 49 chars
     * - Per line (ASCII): 3 (" |") + 16 (ASCII chars) + 1 ("|") = 20 chars
     * - Per line (newline): 2 chars
     * - Continuation prefix: ~30 chars
     * - Safety margin: 64 chars
     */
    DWORD numLines = (len + 15) / 16;
    DWORD bufSize = 64 + numLines * (49 + 20 + 2 + 30) + 64;
    WCHAR *buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufSize * sizeof(WCHAR));
    if (!buf)
        return;

    int pos = 0;
    int maxPos = (int)bufSize - 1; /* Leave room for null terminator */

    /* Timestamp */
    pos += FormatTimestamp(buf + pos, maxPos - pos);

    /* Direction */
    pos += wsprintfW(buf + pos, L"[%s] ", (dir == DIR_RX) ? L"RX" : L"TX");

    int prefixLen = pos;
    int lineStart = 0;      /* Index into data[] for current line start */

    for (DWORD i = 0; i < len && pos < maxPos - 30; i++) {
        /* Line break every 16 bytes */
        if (i > 0 && i % 16 == 0) {
            /* Add ASCII decode for previous line */
            if (pos + 20 < maxPos) {
                buf[pos++] = L' ';
                buf[pos++] = L'|';
                for (int k = lineStart; k < (int)i && pos < maxPos - 2; k++) {
                    buf[pos++] = (data[k] >= 0x20 && data[k] <= 0x7E) ? (WCHAR)data[k] : L'.';
                }
                buf[pos++] = L'|';
            }

            /* Newline */
            if (pos + 2 + prefixLen < maxPos) {
                buf[pos++] = L'\r';
                buf[pos++] = L'\n';

                /* Prefix alignment */
                for (int j = 0; j < prefixLen; j++)
                    buf[pos++] = L' ';
            }

            lineStart = i;
        } else if (i > 0 && i % 8 == 0) {
            /* Extra space at byte 8 */
            if (pos < maxPos)
                buf[pos++] = L' ';
        }

        if (pos + 4 < maxPos)
            pos += wsprintfW(buf + pos, L"%02X ", data[i]);
    }

    /* Add ASCII decode for last line (pad hex to align ASCII) */
    int lastLineLen = (int)len - lineStart;
    if (lastLineLen > 0 && pos + 20 < maxPos) {
        /* Pad hex to align ASCII (full line = 49 hex chars) */
        int hexChars = lastLineLen * 3 + (lastLineLen > 8 ? 1 : 0);
        int targetHexWidth = 49; /* 16*3 + 1 */
        while (hexChars < targetHexWidth && pos < maxPos - 2) {
            buf[pos++] = L' ';
            hexChars++;
        }

        buf[pos++] = L' ';
        buf[pos++] = L'|';
        for (int k = lineStart; k < (int)len && pos < maxPos - 2; k++) {
            buf[pos++] = (data[k] >= 0x20 && data[k] <= 0x7E) ? (WCHAR)data[k] : L'.';
        }
        buf[pos++] = L'|';
    }

    if (pos + 2 < maxPos) {
        buf[pos++] = L'\r';
        buf[pos++] = L'\n';
    }
    buf[pos] = L'\0';

    /* Create entry */
    LOG_ENTRY *entry = (LOG_ENTRY *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LOG_ENTRY));
    if (entry) {
        entry->type = LOG_TYPE_DATA;
        entry->color1 = (dir == DIR_RX) ? COLOR_RX : COLOR_TX;
        entry->text = buf;
        entry->textLen = pos;
        AddEntry(entry);
    } else {
        HeapFree(GetProcessHeap(), 0, buf);
    }
}

/*
 * Main_AppendCustomLog - Format and append custom text to log buffer
 */
void Main_AppendCustomLog(HWND hMainWnd, const WCHAR *tag, const WCHAR *text)
{
    (void)hMainWnd;
    if (!g_initialized || !tag || !text)
        return;
    
    /* Build formatted text: timestamp + tag + text + newline */
    int textLen = lstrlenW(text);
    int maxLen = 64 + 64 + textLen + 4;
    WCHAR *buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, maxLen * sizeof(WCHAR));
    if (!buf)
        return;
    
    int pos = 0;
    pos += FormatTimestamp(buf + pos, maxLen - pos);
    pos += wsprintfW(buf + pos, L"[%s] ", tag);
    CopyMemory(buf + pos, text, textLen * sizeof(WCHAR));
    pos += textLen;
    buf[pos++] = L'\r';
    buf[pos++] = L'\n';
    buf[pos] = L'\0';
    
    LOG_ENTRY *entry = (LOG_ENTRY *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LOG_ENTRY));
    if (entry) {
        entry->type = LOG_TYPE_CUSTOM;
        entry->color1 = COLOR_CUSTOM;
        entry->text = buf;
        entry->textLen = pos;
        AddEntry(entry);
    } else {
        HeapFree(GetProcessHeap(), 0, buf);
    }
}

/*
 * Main_AppendSignalLog - Format and append signal/config log to buffer
 */
void Main_AppendSignalLog(const WCHAR *tag, const WCHAR *text, COLORREF tagColor)
{
    if (!g_initialized || !tag || !text)
        return;
    
    /* Build formatted text: timestamp + tag + text + newline */
    int textLen = lstrlenW(text);
    int maxLen = 64 + 64 + textLen + 4;
    WCHAR *buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, maxLen * sizeof(WCHAR));
    if (!buf)
        return;
    
    int pos = 0;
    pos += FormatTimestamp(buf + pos, maxLen - pos);
    pos += wsprintfW(buf + pos, L"[%s] ", tag);
    CopyMemory(buf + pos, text, textLen * sizeof(WCHAR));
    pos += textLen;
    buf[pos++] = L'\r';
    buf[pos++] = L'\n';
    buf[pos] = L'\0';
    
    LOG_ENTRY *entry = (LOG_ENTRY *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LOG_ENTRY));
    if (entry) {
        entry->type = LOG_TYPE_SIGNAL;
        entry->color1 = tagColor;
        entry->text = buf;
        entry->textLen = pos;
        AddEntry(entry);
    } else {
        HeapFree(GetProcessHeap(), 0, buf);
    }
}
