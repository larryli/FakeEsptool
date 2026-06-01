/*
 * trace.c - Trace logging utility implementation
 */

#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>

#if defined(ENABLE_TRACE_FW) || defined(ENABLE_TRACE_PROTO)

#include "trace.h"

static HANDLE g_hTraceFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_csTrace;

void Trace_Init(void)
{
    InitializeCriticalSection(&g_csTrace);

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

void Trace_Close(void)
{
    if (g_hTraceFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hTraceFile);
        g_hTraceFile = INVALID_HANDLE_VALUE;
    }
    DeleteCriticalSection(&g_csTrace);
}

void Trace_Write(const char *tag, const char *fmt, ...)
{
    if (g_hTraceFile == INVALID_HANDLE_VALUE)
        return;

    EnterCriticalSection(&g_csTrace);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char buf[4096];
    int len = snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d [%lu] [%s] ",
                       st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                       GetCurrentThreadId(), tag ? tag : "?");

    va_list args;
    va_start(args, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - len, fmt, args);
    va_end(args);

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
