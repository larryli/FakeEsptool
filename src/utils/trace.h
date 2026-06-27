/*
 * trace.h - Trace logging utility
 *
 * Provides categorized trace logging to a single log file.
 *
 * Categories (compile-time switches):
 *   ENABLE_TRACE_FW    - Framework traces (GUI, Serial, etc.)
 *   ENABLE_TRACE_PROTO - Protocol implementation traces
 *
 * Enable:
 *   cmake .. -DENABLE_TRACE_FW=ON     # Framework only
 *   cmake .. -DENABLE_TRACE_PROTO=ON  # Protocol only
 *   cmake .. -DENABLE_TRACE_FW=ON -DENABLE_TRACE_PROTO=ON  # Both
 *
 * TAG convention is defined by the user.
 */

#ifndef TRACE_H
#define TRACE_H

#if defined(ENABLE_TRACE_FW) || defined(ENABLE_TRACE_PROTO)

#define ENABLE_TRACE 1

#include <windows.h>

/*
 * Trace_Init - Initialize trace logging
 */
void Trace_Init(void);

/*
 * Trace_Close - Close trace logging
 */
void Trace_Close(void);

/*
 * Trace_Write - Write trace message to log file
 */
void Trace_Write(const char *tag, const char *fmt, ...);

/*
 * Trace_WriteVa - Write trace message using va_list
 *
 * Core implementation. Trace_Write forwards to this.
 */
void Trace_WriteVa(const char *tag, const char *fmt, va_list ap);

#define TRACE_INIT() Trace_Init()
#define TRACE_CLOSE() Trace_Close()

#else

#define TRACE_INIT()
#define TRACE_CLOSE()

#endif /* ENABLE_TRACE */

/* Framework trace */
#ifdef ENABLE_TRACE_FW
#define TRACE_FW(tag, ...) Trace_Write(tag, __VA_ARGS__)
#else
#define TRACE_FW(tag, ...)
#endif

/* Protocol trace */
#ifdef ENABLE_TRACE_PROTO
#define TRACE_PROTO(tag, ...) Trace_Write(tag, __VA_ARGS__)
#else
#define TRACE_PROTO(tag, ...)
#endif

#endif /* TRACE_H */
