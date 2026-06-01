/*
 * config.c - Configuration file implementation
 *
 * Uses Windows INI file API for persistent configuration.
 * Config file is stored in the same directory as the executable.
 */

#include "config.h"
#include <wchar.h>
#include <stdio.h>

static WCHAR g_iniPath[MAX_PATH] = {0};

/* Section and key names */
#define SECTION_FONT    L"Font"
#define KEY_FONT_NAME   L"Name"
#define KEY_FONT_SIZE   L"Size"
#define KEY_FONT_WEIGHT L"Weight"

#define SECTION_PORT    L"Port"
#define KEY_LAST_PORT   L"LastPort"

/*
 * Config_Init - Initialize config module
 */
void Config_Init(void)
{
    GetModuleFileNameW(NULL, g_iniPath, MAX_PATH);

    /* Replace .exe with .ini */
    WCHAR *ext = wcsrchr(g_iniPath, L'.');
    if (ext)
        lstrcpyW(ext, L".ini");
}

/*
 * Config_GetFont - Get saved font settings
 */
BOOL Config_GetFont(LOGFONTW *lf)
{
    if (!g_iniPath[0])
        return FALSE;

    WCHAR name[LF_FACESIZE] = {0};
    GetPrivateProfileStringW(SECTION_FONT, KEY_FONT_NAME, L"",
                             name, LF_FACESIZE, g_iniPath);

    if (name[0] == L'\0')
        return FALSE;

    int size = GetPrivateProfileIntW(SECTION_FONT, KEY_FONT_SIZE, 0, g_iniPath);
    int weight = GetPrivateProfileIntW(SECTION_FONT, KEY_FONT_WEIGHT, 0, g_iniPath);

    if (size <= 0)
        return FALSE;

    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    lf->lfHeight = -MulDiv(size, dpi, 72);  /* Convert point size to pixels */
    lf->lfWeight = weight;
    lf->lfCharSet = DEFAULT_CHARSET;
    lf->lfOutPrecision = OUT_TT_PRECIS;
    lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf->lfQuality = CLEARTYPE_QUALITY;
    lf->lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lstrcpyW(lf->lfFaceName, name);

    return TRUE;
}

/*
 * Config_SetFont - Save font settings
 */
void Config_SetFont(const LOGFONTW *lf)
{
    if (!g_iniPath[0])
        return;

    /* Calculate point size from pixel height using actual DPI */
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    int size = MulDiv(-lf->lfHeight, 72, dpi);

    WCHAR buf[32];
    wsprintfW(buf, L"%d", size);
    WritePrivateProfileStringW(SECTION_FONT, KEY_FONT_NAME, lf->lfFaceName, g_iniPath);
    WritePrivateProfileStringW(SECTION_FONT, KEY_FONT_SIZE, buf, g_iniPath);
    wsprintfW(buf, L"%d", lf->lfWeight);
    WritePrivateProfileStringW(SECTION_FONT, KEY_FONT_WEIGHT, buf, g_iniPath);
}

/*
 * Config_GetLastPort - Get last connected port name
 */
BOOL Config_GetLastPort(WCHAR *portName, int maxLen)
{
    if (!g_iniPath[0])
        return FALSE;

    GetPrivateProfileStringW(SECTION_PORT, KEY_LAST_PORT, L"",
                             portName, maxLen, g_iniPath);

    return (portName[0] != L'\0');
}

/*
 * Config_SetLastPort - Save last connected port name
 */
void Config_SetLastPort(const WCHAR *portName)
{
    if (!g_iniPath[0])
        return;

    WritePrivateProfileStringW(SECTION_PORT, KEY_LAST_PORT, portName, g_iniPath);
}

/*
 * Config_GetLastDeviceFile - Get last opened device file path
 */
BOOL Config_GetLastDeviceFile(WCHAR *filePath, int maxLen)
{
    if (!g_iniPath[0])
        return FALSE;

    GetPrivateProfileStringW(L"Device", L"LastFile", L"",
                             filePath, maxLen, g_iniPath);

    return (filePath[0] != L'\0');
}

/*
 * Config_SetLastDeviceFile - Save last opened device file path
 */
void Config_SetLastDeviceFile(const WCHAR *filePath)
{
    if (!g_iniPath[0])
        return;

    WritePrivateProfileStringW(L"Device", L"LastFile", filePath ? filePath : L"", g_iniPath);
}

/*
 * Config_GetString - Get string value from config
 */
BOOL Config_GetString(const WCHAR *section, const WCHAR *key, WCHAR *value, int maxLen, const WCHAR *defaultVal)
{
    if (!g_iniPath[0] || !section || !key || !value || maxLen <= 0)
        return FALSE;

    GetPrivateProfileStringW(section, key, defaultVal ? defaultVal : L"",
                             value, maxLen, g_iniPath);

    return (value[0] != L'\0');
}

/*
 * Config_SetString - Save string value to config
 */
void Config_SetString(const WCHAR *section, const WCHAR *key, const WCHAR *value)
{
    if (!g_iniPath[0] || !section || !key)
        return;

    WritePrivateProfileStringW(section, key, value ? value : L"", g_iniPath);
}

/*
 * Config_GetInt - Get integer value from config
 */
int Config_GetInt(const WCHAR *section, const WCHAR *key, int defaultVal)
{
    if (!g_iniPath[0] || !section || !key)
        return defaultVal;

    return (int)GetPrivateProfileIntW(section, key, defaultVal, g_iniPath);
}

/*
 * Config_SetInt - Save integer value to config
 */
void Config_SetInt(const WCHAR *section, const WCHAR *key, int value)
{
    if (!g_iniPath[0] || !section || !key)
        return;

    WCHAR buf[32];
    wsprintfW(buf, L"%d", value);
    WritePrivateProfileStringW(section, key, buf, g_iniPath);
}

/*
 * Config_GetBool - Get boolean value from config
 */
BOOL Config_GetBool(const WCHAR *section, const WCHAR *key, BOOL defaultVal)
{
    return Config_GetInt(section, key, defaultVal ? 1 : 0) != 0;
}

/*
 * Config_SetBool - Save boolean value to config
 */
void Config_SetBool(const WCHAR *section, const WCHAR *key, BOOL value)
{
    Config_SetInt(section, key, value ? 1 : 0);
}
