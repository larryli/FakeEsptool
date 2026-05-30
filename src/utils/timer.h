/*
 * timer.h - Timer utility interface
 *
 * Provides a simple timer API for protocol layer timeout handling.
 * Uses Windows waitable timers for precise timing.
 *
 * Usage:
 *   TIMER_CTX *ctx = Timer_Create();
 *   Timer_Start(ctx, 5000, MyCallback, userData);  // 5 second timeout
 *   // ... later ...
 *   Timer_Cancel(ctx);
 *   Timer_Destroy(ctx);
 */

#ifndef TIMER_H
#define TIMER_H

#include <windows.h>

/* Timer callback type
 * @userData: User data passed to Timer_Start
 */
typedef void (*TIMER_CB)(void *userData);

/* Timer context (opaque) */
typedef struct TIMER_CTX TIMER_CTX;

/*
 * Timer_Create - Create a new timer context
 * Returns: Timer context, or NULL on failure
 */
TIMER_CTX *Timer_Create(void);

/*
 * Timer_Destroy - Destroy a timer context
 * @ctx: Timer context
 */
void Timer_Destroy(TIMER_CTX *ctx);

/*
 * Timer_Start - Start a one-shot timer
 *
 * If timer is already running, it will be restarted.
 * Callback is called from a worker thread, not the UI thread.
 *
 * @ctx: Timer context
 * @timeoutMs: Timeout in milliseconds
 * @cb: Callback function (called when timeout expires)
 * @userData: User data passed to callback
 * Returns: TRUE on success
 */
BOOL Timer_Start(TIMER_CTX *ctx, DWORD timeoutMs, TIMER_CB cb, void *userData);

/*
 * Timer_Cancel - Cancel a running timer
 * @ctx: Timer context
 */
void Timer_Cancel(TIMER_CTX *ctx);

/*
 * Timer_IsRunning - Check if timer is running
 * @ctx: Timer context
 * Returns: TRUE if timer is running
 */
BOOL Timer_IsRunning(TIMER_CTX *ctx);

#endif /* TIMER_H */
