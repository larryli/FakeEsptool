/*
 * main.h - Application interface
 *
 * Defines custom window messages for serial thread communication
 * and application initialization functions.
 */

#ifndef MAIN_H
#define MAIN_H

#include <windows.h>

/* Custom window messages from serial/protocol layer to GUI */
#define WM_SERIAL_RX      (WM_USER + 1)  /* RX data: wParam=len, lParam=HeapAlloc ptr */
#define WM_SERIAL_TX      (WM_USER + 2)  /* TX data: wParam=len, lParam=HeapAlloc ptr */
#define WM_SERIAL_ERROR   (WM_USER + 3)  /* Connection error: wParam=error code */
#define WM_SERIAL_LOG     (WM_USER + 4)  /* Custom log: wParam=WCHAR* tag, lParam=WCHAR* text */
#define WM_SERIAL_SIGNAL  (WM_USER + 5)  /* Signal change: wParam=modemStatus */
#define WM_SERIAL_CONFIG  (WM_USER + 6)  /* Config change: wParam=baudRate, lParam=config flags */

/* Custom message for application initialization */
#define WM_APP_INIT       (WM_USER + 100)

#endif /* MAIN_H */
