/*
 * mem.h - Memory management utilities
 *
 * Provides wrappers around Windows Heap API for the protocol and device
 * layers. Simplifies allocation calls and adds optional leak tracking
 * in debug builds.
 */

#ifndef UTILS_MEM_H
#define UTILS_MEM_H

#include <windows.h>

/*
 * Mem_Alloc - Allocate memory block
 *
 * Equivalent to HeapAlloc(GetProcessHeap(), 0, size).
 * Returns NULL on failure.
 */
void *Mem_Alloc(DWORD size);

/*
 * Mem_ZeroAlloc - Allocate zero-initialized memory block
 *
 * Equivalent to HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size).
 * Returns NULL on failure.
 */
void *Mem_ZeroAlloc(DWORD size);

/*
 * Mem_Realloc - Reallocate memory block
 *
 * Equivalent to HeapReAlloc(GetProcessHeap(), 0, ptr, size).
 * If ptr is NULL, behaves like Mem_Alloc.
 * Returns NULL on failure (original block is NOT freed).
 */
void *Mem_Realloc(void *ptr, DWORD size);

/*
 * Mem_Free - Free allocated memory block
 *
 * Equivalent to HeapFree(GetProcessHeap(), 0, ptr).
 * Safe to call with NULL pointer (no-op).
 */
void Mem_Free(void *ptr);

#ifdef ENABLE_MEM_DEBUG

/*
 * Mem_ReportLeaks - Report unfreed allocations
 *
 * Call at program exit to list all memory blocks that were not freed.
 * Outputs to debug output (OutputDebugString).
 * Returns the number of leaked allocations.
 */
int Mem_ReportLeaks(void);

/*
 * Mem_GetAllocCount - Get total active allocation count
 *
 * Returns the number of currently unfreed allocations.
 */
DWORD Mem_GetAllocCount(void);

/*
 * Mem_GetTotalAllocSize - Get total active allocation size
 *
 * Returns total bytes of currently unfreed allocations.
 */
DWORD Mem_GetTotalAllocSize(void);

#else

#define Mem_ReportLeaks() (0)
#define Mem_GetAllocCount() ((DWORD)0)
#define Mem_GetTotalAllocSize() ((DWORD)0)

#endif /* ENABLE_MEM_DEBUG */

#endif /* UTILS_MEM_H */
