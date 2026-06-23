/*
 * trace.c - Trace logging utility implementation
 *
 * Provides categorized trace logging to a log file.
 * Log file is created in the same directory as the executable.
 */

#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>

#if defined(ENABLE_TRACE_FW) || defined(ENABLE_TRACE_PROTO)

#include "trace.h"

static HANDLE g_hTraceFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_csTrace;
static LARGE_INTEGER g_traceFreq = {0};        /* Performance counter frequency */
static LARGE_INTEGER g_lastTraceCounter = {0}; /* Last performance counter value */

/*
 * Trace_Init - Initialize trace logging
 *
 * Creates log file and initializes critical section.
 * Log file path is same as executable with .log extension.
 */
void Trace_Init(void)
{
    InitializeCriticalSection(&g_csTrace);

    /* Initialize high-precision timing */
    QueryPerformanceFrequency(&g_traceFreq);
    QueryPerformanceCounter(&g_lastTraceCounter);

    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);

    /* Replace .exe with .log */
    WCHAR *ext = wcsrchr(path, L'.');
    if (ext)
        lstrcpyW(ext, L".log");

    g_hTraceFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (g_hTraceFile != INVALID_HANDLE_VALUE) {
        /* Write UTF-8 BOM */
        DWORD written;
        BYTE bom[] = {0xEF, 0xBB, 0xBF};
        WriteFile(g_hTraceFile, bom, 3, &written, NULL);
    }
}

/*
 * Trace_Close - Close trace logging
 *
 * Closes log file and releases critical section.
 */
void Trace_Close(void)
{
    if (g_hTraceFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hTraceFile);
        g_hTraceFile = INVALID_HANDLE_VALUE;
    }
    DeleteCriticalSection(&g_csTrace);
}

/*
 * Trace_Write - Write trace message to log file
 *
 * Thread-safe function that writes timestamped message with tag.
 * Format: "HH:MM:SS.mmm +X.XXX [thread_id] [tag] message\r\n"
 *
 * @tag: Category tag (e.g. "GUI", "ESP", "SER")
 * @fmt: printf-style format string
 * @...: Format arguments
 */
void Trace_Write(const char *tag, const char *fmt, ...)
{
    if (g_hTraceFile == INVALID_HANDLE_VALUE)
        return;

    EnterCriticalSection(&g_csTrace);

    SYSTEMTIME st;
    GetLocalTime(&st);

    /* Calculate relative time delta using high-precision counter */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    DWORD deltaMs = 0;
    if (g_traceFreq.QuadPart != 0 && g_lastTraceCounter.QuadPart != 0) {
        LONGLONG deltaTicks = now.QuadPart - g_lastTraceCounter.QuadPart;
        deltaMs = (DWORD)(deltaTicks * 1000 / g_traceFreq.QuadPart);
    }
    g_lastTraceCounter = now;

    char buf[4096];
    int len = snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d +%lu.%03lu [%lu] [%s] ",
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                       deltaMs / 1000, deltaMs % 1000,
                       GetCurrentThreadId(), tag ? tag : "?");

    va_list args;
    va_start(args, fmt);
    int msgLen = vsnprintf(buf + len, sizeof(buf) - len, fmt, args);
    va_end(args);

    /* vsnprintf returns number of chars that would be written (excluding null).
       Clamp len to actual buffer size to prevent out-of-bounds access. */
    if (msgLen >= 0) {
        len += msgLen;
        if (len >= (int)sizeof(buf))
            len = (int)sizeof(buf) - 1;
    }

    if (len > 0 && len < (int)sizeof(buf) - 2) {
        buf[len++] = '\r';
        buf[len++] = '\n';
    }

    DWORD written;
    WriteFile(g_hTraceFile, buf, len, &written, NULL);
    FlushFileBuffers(g_hTraceFile);

    LeaveCriticalSection(&g_csTrace);
}

#endif /* ENABLE_TRACE_FW || ENABLE_TRACE_PROTO */
