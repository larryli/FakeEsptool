/*
 * config.h - Configuration file interface
 *
 * Provides functions for reading/writing INI configuration file.
 * Generic functions allow protocol layer to store custom settings.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

/*
 * Config_Init - Initialize config module, load config file path
 */
void Config_Init(void);

/*
 * Config_GetFont - Get saved font settings
 * @lf: Pointer to LOGFONTW structure to receive settings
 * Returns: TRUE if settings were loaded
 */
BOOL Config_GetFont(LOGFONTW *lf);

/*
 * Config_SetFont - Save font settings
 * @lf: Pointer to LOGFONTW structure with settings
 */
void Config_SetFont(const LOGFONTW *lf);

/*
 * Config_GetLastPort - Get last connected port name
 * @portName: Buffer to receive port name
 * @maxLen: Buffer size in characters
 * Returns: TRUE if port name was loaded
 */
BOOL Config_GetLastPort(WCHAR *portName, int maxLen);

/*
 * Config_SetLastPort - Save last connected port name
 * @portName: Port name to save
 */
void Config_SetLastPort(const WCHAR *portName);

/*
 * Config_GetLastDeviceFile - Get last opened device file path
 * @filePath: Buffer to receive file path
 * @maxLen: Buffer size in characters
 * Returns: TRUE if file path was loaded
 */
BOOL Config_GetLastDeviceFile(WCHAR *filePath, int maxLen);

/*
 * Config_SetLastDeviceFile - Save last opened device file path
 * @filePath: File path to save (NULL to clear)
 */
void Config_SetLastDeviceFile(const WCHAR *filePath);

/*
 * Config_GetString - Get string value from config
 * @section: Section name
 * @key: Key name
 * @value: Buffer to receive value
 * @maxLen: Buffer size in characters
 * @defaultVal: Default value if key not found
 * Returns: TRUE if value was loaded
 */
BOOL Config_GetString(const WCHAR *section, const WCHAR *key, WCHAR *value,
                      int maxLen, const WCHAR *defaultVal);

/*
 * Config_SetString - Save string value to config
 * @section: Section name
 * @key: Key name
 * @value: Value to save
 */
void Config_SetString(const WCHAR *section, const WCHAR *key,
                      const WCHAR *value);

/*
 * Config_GetInt - Get integer value from config
 * @section: Section name
 * @key: Key name
 * @defaultVal: Default value if key not found
 * Returns: Integer value
 */
int Config_GetInt(const WCHAR *section, const WCHAR *key, int defaultVal);

/*
 * Config_SetInt - Save integer value to config
 * @section: Section name
 * @key: Key name
 * @value: Value to save
 */
void Config_SetInt(const WCHAR *section, const WCHAR *key, int value);

/*
 * Config_GetBool - Get boolean value from config
 * @section: Section name
 * @key: Key name
 * @defaultVal: Default value if key not found
 * Returns: Boolean value
 */
BOOL Config_GetBool(const WCHAR *section, const WCHAR *key, BOOL defaultVal);

/*
 * Config_SetBool - Save boolean value to config
 * @section: Section name
 * @key: Key name
 * @value: Value to save (TRUE/FALSE)
 */
void Config_SetBool(const WCHAR *section, const WCHAR *key, BOOL value);

#endif /* CONFIG_H */
