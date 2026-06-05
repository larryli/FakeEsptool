/*
 * app_logview.h - Log display functions
 */

#ifndef APP_LOGVIEW_H
#define APP_LOGVIEW_H

#include <windows.h>

/* Color definitions for log display */
#define COLOR_TIMESTAMP RGB(128, 128, 128)  /* Gray */
#define COLOR_RX        RGB(0, 0, 200)      /* Blue */
#define COLOR_TX        RGB(0, 128, 0)      /* Green */
#define COLOR_SIGNAL    RGB(128, 0, 128)    /* Purple */
#define COLOR_CONFIG    RGB(0, 128, 128)    /* Teal */
#define COLOR_CUSTOM    RGB(200, 100, 0)    /* Orange */
#define COLOR_DATA      RGB(0, 0, 0)        /* Black */
#define COLOR_BG        RGB(240, 240, 240)  /* Light gray */

/* Global edit control handle (defined in main.c) */
extern HWND g_hEdit;

/* Format and append RX/TX data to log display */
void Main_AppendLog(HWND hMainWnd, const BYTE *data, DWORD len, int dir);

/* Format and append custom text to log display */
void Main_AppendCustomLog(HWND hMainWnd, const WCHAR *tag, const WCHAR *text);

/* Format and append signal/config log with distinct colors */
void Main_AppendSignalLog(const WCHAR *tag, const WCHAR *text, COLORREF tagColor);

#endif /* APP_LOGVIEW_H */
