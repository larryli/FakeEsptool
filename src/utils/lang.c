/*
 * lang.c - Localization helper implementation
 *
 * Uses a round-robin pool of static buffers so that multiple LoadStr()
 * calls in the same expression (e.g. MessageBoxW caption + body) do not
 * clobber each other.
 */

#include "lang.h"

#define STR_POOL_SIZE 4
#define STR_BUF_LEN 256

static WCHAR g_pool[STR_POOL_SIZE][STR_BUF_LEN];
static int g_idx = 0;

/*
 * LoadStr - Load localized string by ID
 */
const WCHAR *LoadStr(UINT id)
{
    /* Rotate to next buffer in pool */
    WCHAR *buf = g_pool[g_idx];
    g_idx = (g_idx + 1) % STR_POOL_SIZE;

    int len = LoadStringW(GetModuleHandleW(NULL), id, buf, STR_BUF_LEN);
    if (len > 0) {
        return buf;
    }
    return L"";
}
