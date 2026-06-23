/*
 * app_logview.h - Log display functions
 *
 * Provides buffered, batch-updated colored log display in RichEdit control.
 * Uses timer-based batch updates with WM_SETREDRAW for smooth performance.
 */

#ifndef APP_LOGVIEW_H
#define APP_LOGVIEW_H

#include <windows.h>

/* Color definitions for log display */
#define COLOR_TIMESTAMP RGB(128, 128, 128) /* Gray */
#define COLOR_RX RGB(0, 0, 200)            /* Blue */
#define COLOR_TX RGB(0, 128, 0)            /* Green */
#define COLOR_SIGNAL RGB(128, 0, 128)      /* Purple */
#define COLOR_CONFIG RGB(0, 128, 128)      /* Teal */
#define COLOR_CUSTOM RGB(200, 100, 0)      /* Orange */
#define COLOR_DATA RGB(0, 0, 0)            /* Black */
#define COLOR_BG RGB(240, 240, 240)        /* Light gray */

/* Timer ID for log flush */
#define LOG_FLUSH_TIMER_ID 1001
#define LOG_FLUSH_INTERVAL_MS 50 /* Flush every 50ms */

/* Global edit control handle (defined in main.c) */
extern HWND g_hEdit;

/*
 * LogView_Init - Initialize log view subsystem
 *
 * Creates timer for batch updates. Call once at startup.
 *
 * @hWnd: Main window handle
 */
void LogView_Init(HWND hWnd);

/*
 * LogView_Close - Shutdown log view subsystem
 *
 * Flushes remaining entries and frees resources. Call at exit.
 */
void LogView_Close(void);

/*
 * LogView_FlushTimer - Timer callback for batch log flush
 *
 * Called by WM_TIMER. Flushes buffered log entries to RichEdit.
 * Call this from Main_OnTimer in main.c.
 */
void LogView_FlushTimer(void);

/*
 * LogView_Flush - Flush all buffered log entries immediately
 *
 * Forces immediate flush of all pending log entries.
 * Useful before showing dialogs or during shutdown.
 */
void LogView_Flush(void);

/*
 * Main_AppendLog - Format and append RX/TX data to log display
 */
void Main_AppendLog(HWND hMainWnd, const BYTE *data, DWORD len, int dir);

/*
 * Main_AppendCustomLog - Format and append custom text to log display
 */
void Main_AppendCustomLog(HWND hMainWnd, const WCHAR *tag, const WCHAR *text);

/*
 * Main_AppendSignalLog - Format and append signal/config log with distinct
 * colors
 */
void Main_AppendSignalLog(const WCHAR *tag, const WCHAR *text,
                          COLORREF tagColor);

#endif /* APP_LOGVIEW_H */
