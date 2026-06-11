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

/*
 * FormatTimestamp - Format current time as timestamp string
 */
static int FormatTimestamp(WCHAR *buf, int maxLen)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return wsprintfW(buf, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
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
 */
void Main_AppendLog(HWND hMainWnd, const BYTE *data, DWORD len, int dir)
{
    (void)hMainWnd;
    if (!g_initialized || len == 0)
        return;
    
    /* Build formatted text: timestamp + direction + hex data */
    DWORD numLines = (len + 15) / 16;
    DWORD maxLineSize = 64 + 16 + (len * 4) + (numLines * 64) + 64;
    WCHAR *buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, maxLineSize * sizeof(WCHAR));
    if (!buf)
        return;
    
    int pos = 0;
    
    /* Timestamp */
    pos += FormatTimestamp(buf + pos, maxLineSize - pos);
    
    /* Direction */
    pos += wsprintfW(buf + pos, L"[%s] ", (dir == DIR_RX) ? L"RX" : L"TX");
    
    /* Hex data with grouping */
    int prefixLen = pos;
    for (DWORD i = 0; i < len; i++) {
        if (i > 0 && i % 16 == 0) {
            buf[pos++] = L'\r';
            buf[pos++] = L'\n';
            for (int j = 0; j < prefixLen; j++)
                buf[pos++] = L' ';
        } else if (i > 0 && i % 8 == 0) {
            buf[pos++] = L' ';
        }
        pos += wsprintfW(buf + pos, L"%02X ", data[i]);
    }
    buf[pos++] = L'\r';
    buf[pos++] = L'\n';
    buf[pos] = L'\0';
    
    /* Create entry: timestamp color first, then direction color */
    /* We need 3 entries: timestamp (gray) + direction (color) + data (black) */
    
    /* For simplicity, create one entry with the entire text.
       Color will be the direction color for the whole line. */
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
