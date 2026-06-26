/*
 * mem.c - Memory management utilities implementation
 *
 * Wraps Windows Heap API with optional allocation tracking for debugging.
 */

#include "mem.h"

#ifdef ENABLE_MEM_DEBUG

#include <stdio.h>

/* Allocation tracking entry */
typedef struct AllocEntry {
    void *ptr;
    DWORD size;
    const char *file;
    int line;
    struct AllocEntry *next;
} AllocEntry;

static AllocEntry *alloc_list = NULL;
static DWORD alloc_count = 0;
static DWORD alloc_total_size = 0;

static void TrackAlloc(void *ptr, DWORD size, const char *file, int line)
{
    AllocEntry *entry =
        (AllocEntry *)HeapAlloc(GetProcessHeap(), 0, sizeof(AllocEntry));
    if (entry) {
        entry->ptr = ptr;
        entry->size = size;
        entry->file = file;
        entry->line = line;
        entry->next = alloc_list;
        alloc_list = entry;
        alloc_count++;
        alloc_total_size += size;
    }
}

static void UntrackAlloc(void *ptr)
{
    AllocEntry **pp = &alloc_list;
    while (*pp) {
        if ((*pp)->ptr == ptr) {
            AllocEntry *found = *pp;
            *pp = found->next;
            alloc_count--;
            alloc_total_size -= found->size;
            HeapFree(GetProcessHeap(), 0, found);
            return;
        }
        pp = &(*pp)->next;
    }
}

int Mem_ReportLeaks(void)
{
    int leak_count = 0;
    AllocEntry *entry = alloc_list;
    while (entry) {
        char buf[256];
        wsprintfA(buf, "[MEM LEAK] %p %lu bytes at %s:%d\n", entry->ptr,
                  entry->size, entry->file, entry->line);
        OutputDebugStringA(buf);
        leak_count++;
        entry = entry->next;
    }
    return leak_count;
}

DWORD Mem_GetAllocCount(void) { return alloc_count; }

DWORD Mem_GetTotalAllocSize(void) { return alloc_total_size; }

#define TRACK(ptr, size) TrackAlloc(ptr, size, __FILE__, __LINE__)
#define UNTRACK(ptr) UntrackAlloc(ptr)

#else

#define TRACK(ptr, size) ((void)0)
#define UNTRACK(ptr) ((void)0)

#endif /* ENABLE_MEM_DEBUG */

void *Mem_Alloc(DWORD size)
{
    void *ptr = HeapAlloc(GetProcessHeap(), 0, size);
    TRACK(ptr, size);
    return ptr;
}

void *Mem_ZeroAlloc(DWORD size)
{
    void *ptr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    TRACK(ptr, size);
    return ptr;
}

void *Mem_Realloc(void *ptr, DWORD size)
{
    if (!ptr) {
        return Mem_Alloc(size);
    }
    UNTRACK(ptr);
    void *new_ptr = HeapReAlloc(GetProcessHeap(), 0, ptr, size);
    TRACK(new_ptr, size);
    return new_ptr;
}

void Mem_Free(void *ptr)
{
    if (ptr) {
        UNTRACK(ptr);
        HeapFree(GetProcessHeap(), 0, ptr);
    }
}
