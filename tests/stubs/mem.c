/*
 * mem.c - Minimal stub for unit testing
 *
 * Directly wraps stdlib malloc/free/realloc.
 */

#include <stdlib.h>
#include <string.h>

void *Mem_Alloc(unsigned long size) { return malloc(size); }

void *Mem_ZeroAlloc(unsigned long size)
{
    void *p = malloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void Mem_Free(void *ptr) { free(ptr); }
