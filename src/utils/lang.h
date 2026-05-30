/*
 * lang.h - Localization helper interface
 *
 * Provides LoadStr() function for loading localized strings.
 */

#ifndef LANG_H
#define LANG_H

#include <windows.h>

/*
 * LoadStr - Load localized string by ID
 *
 * @id: String resource ID
 * Returns: Pointer to static buffer with string, or L"" if not found
 */
const WCHAR *LoadStr(UINT id);

#endif /* LANG_H */
