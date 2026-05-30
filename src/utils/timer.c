/*
 * timer.c - Timer utility implementation
 *
 * Uses Windows waitable timers for precise timeout handling.
 * Callbacks are executed from a worker thread.
 */

#include "timer.h"
#include "trace.h"

static const char *TAG = "TIMER";

/* Timer context structure */
struct TIMER_CTX {
    HANDLE hTimer;          /* Waitable timer handle */
    HANDLE hShutdownEvent;  /* Shutdown signal event */
    HANDLE hThread;         /* Worker thread handle */
    volatile BOOL bRunning; /* Thread running flag */
    TIMER_CB callback;      /* User callback function */
    void *userData;         /* User data for callback */
};

/*
 * Timer_ThreadProc - Worker thread that waits for timer signal
 */
static DWORD WINAPI Timer_ThreadProc(LPVOID param)
{
    TIMER_CTX *ctx = (TIMER_CTX *)param;
    HANDLE handles[2] = { ctx->hTimer, ctx->hShutdownEvent };

    TRACE_FW(TAG, "Timer thread started");

    /* Wait for either timer signal or shutdown */
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

    if (result == WAIT_OBJECT_0 && ctx->bRunning && ctx->callback) {
        TRACE_FW(TAG, "Timer fired, calling callback");
        ctx->callback(ctx->userData);
    }

    TRACE_FW(TAG, "Timer thread exiting");
    return 0;
}

/* Create a new timer context */
TIMER_CTX *Timer_Create(void)
{
    TIMER_CTX *ctx = (TIMER_CTX *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TIMER_CTX));
    if (!ctx)
        return NULL;

    ctx->hTimer = CreateWaitableTimerW(NULL, TRUE, NULL);
    if (!ctx->hTimer) {
        TRACE_FW(TAG, "ERROR: CreateWaitableTimer failed: %lu", GetLastError());
        HeapFree(GetProcessHeap(), 0, ctx);
        return NULL;
    }

    ctx->hShutdownEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ctx->hShutdownEvent) {
        TRACE_FW(TAG, "ERROR: CreateEvent failed: %lu", GetLastError());
        CloseHandle(ctx->hTimer);
        HeapFree(GetProcessHeap(), 0, ctx);
        return NULL;
    }

    ctx->bRunning = FALSE;
    ctx->hThread = NULL;
    ctx->callback = NULL;
    ctx->userData = NULL;

    TRACE_FW(TAG, "Timer context created");
    return ctx;
}

/* Destroy a timer context */
void Timer_Destroy(TIMER_CTX *ctx)
{
    if (!ctx)
        return;

    /* Cancel any running timer */
    Timer_Cancel(ctx);

    if (ctx->hTimer) {
        CloseHandle(ctx->hTimer);
        ctx->hTimer = NULL;
    }
    if (ctx->hShutdownEvent) {
        CloseHandle(ctx->hShutdownEvent);
        ctx->hShutdownEvent = NULL;
    }

    HeapFree(GetProcessHeap(), 0, ctx);
    TRACE_FW(TAG, "Timer context destroyed");
}

/* Start a one-shot timer */
BOOL Timer_Start(TIMER_CTX *ctx, DWORD timeoutMs, TIMER_CB cb, void *userData)
{
    if (!ctx || !ctx->hTimer || !cb || timeoutMs == 0)
        return FALSE;

    /* Cancel any existing timer */
    Timer_Cancel(ctx);

    /* Reset shutdown event */
    ResetEvent(ctx->hShutdownEvent);

    ctx->callback = cb;
    ctx->userData = userData;
    ctx->bRunning = TRUE;

    /* Start worker thread */
    ctx->hThread = CreateThread(NULL, 0, Timer_ThreadProc, ctx, 0, NULL);
    if (!ctx->hThread) {
        TRACE_FW(TAG, "ERROR: CreateThread failed: %lu", GetLastError());
        ctx->bRunning = FALSE;
        return FALSE;
    }

    /* Set timer to fire after timeoutMs */
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -(LONGLONG)timeoutMs * 10000;  /* Convert to 100ns units (negative = relative) */

    if (!SetWaitableTimer(ctx->hTimer, &dueTime, 0, NULL, NULL, FALSE)) {
        TRACE_FW(TAG, "ERROR: SetWaitableTimer failed: %lu", GetLastError());
        Timer_Cancel(ctx);
        return FALSE;
    }

    TRACE_FW(TAG, "Timer started: %lu ms", timeoutMs);
    return TRUE;
}

/* Cancel a running timer */
void Timer_Cancel(TIMER_CTX *ctx)
{
    if (!ctx)
        return;

    if (ctx->bRunning) {
        ctx->bRunning = FALSE;

        /* Signal shutdown and cancel timer */
        SetEvent(ctx->hShutdownEvent);
        if (ctx->hTimer)
            CancelWaitableTimer(ctx->hTimer);

        /* Wait for thread to exit */
        if (ctx->hThread) {
            WaitForSingleObject(ctx->hThread, 1000);
            CloseHandle(ctx->hThread);
            ctx->hThread = NULL;
        }

        ctx->callback = NULL;
        ctx->userData = NULL;
        TRACE_FW(TAG, "Timer cancelled");
    }
}

/* Check if timer is running */
BOOL Timer_IsRunning(TIMER_CTX *ctx)
{
    return (ctx && ctx->bRunning);
}
