/*
 * resource.h - Resource ID definitions
 */

#ifndef RESOURCE_H
#define RESOURCE_H

/* Main window control IDs */
#define IDC_MAIN_EDIT       1001
#define IDC_MAIN_TOOLBAR    1002
#define IDC_MAIN_STATUSBAR  1003

/* Menu command IDs */
#define IDM_NEW_DEVICE      2000
#define IDM_OPEN_DEVICE     2001
#define IDM_SAVE_DEVICE     2002
#define IDM_SAVE_DEVICE_AS  2003
#define IDM_DEVICE_PROPS    2004
#define IDM_CONNECT         2005
#define IDM_DISCONNECT      2006
#define IDM_RECONNECT       2007
#define IDM_FLASH_IMPORT    2008
#define IDM_FLASH_EXPORT    2009
#define IDM_DUMP_DEVICE_AS  2010
#define IDM_LOG_CLEAR       2011
#define IDM_LOG_SAVEAS      2012
#define IDM_LOG_FONT        2013
#define IDM_EXIT            2014
#define IDM_ABOUT           2015

/* Dialog IDs */
#define IDD_PORT_SELECT     3001
#define IDC_PORT_COMBO      3002
#define IDD_ABOUT           3003
#define IDD_DEVICE_PROPS    3004

/* About dialog controls */
#define IDD_APPNAME         3101
#define IDD_COPYRIGHT       3102
#define IDD_WEBSITE         3103

/* Device Properties dialog controls */
#define IDC_CHIP_COMBO      3201
#define IDC_FLASH_SIZE_COMBO 3202
#define IDC_MAC_EDIT        3203
#define IDC_RANDOM_MAC      3204
#define IDC_XTAL_FREQ_COMBO 3205

/* Icon and menu resource IDs */
#define IDI_APP             5001
#define IDR_MAIN_MENU       6001
#define IDR_MAIN_ACCEL      6002

/* Toolbar bitmap resources */
#define IDB_TOOLBAR         7001

/* String IDs */
#define IDS_APP_NAME        10001
#define IDS_DISCONNECTED    10002
#define IDS_TITLE_FORMAT    10003  /* "FakeEsptool - %s" */

/* Tooltip strings */
#define IDS_TIP_CONNECT     10030
#define IDS_TIP_DISCONNECT  10031
#define IDS_TIP_RECONNECT   10032
#define IDS_TIP_CLEAR       10033
#define IDS_TIP_SAVEAS      10034
#define IDS_TIP_DEVPROPS    10035

/* Message strings */
#define IDS_MSG_CONN_TITLE  10061
#define IDS_MSG_CONFIRM_EXIT 10062
#define IDS_MSG_CONFIRM_CAP 10063
#define IDS_MSG_PORT_ERROR  10064
#define IDS_MSG_ERROR       10065
#define IDS_MSG_SELECT_PORT 10066
#define IDS_MSG_WARNING     10067
#define IDS_MSG_INVALID_PORT 10068
#define IDS_MSG_SAVE_ERROR  10069
#define IDS_MSG_DEV_REMOVED 10070
#define IDS_MSG_DEV_TITLE   10071
#define IDS_LOG_SAVE_FILTER 10072
#define IDS_MSG_CONN_LOST   10074
#define IDS_DEVICE_FILTER   10075
#define IDS_BIN_FILTER      10076
#define IDS_FLASH_SIZE_MISMATCH 10077
#define IDS_MSG_CONFIRM_DISCONN 10078
#define IDS_MSG_DISCONN_CAP     10079
#define IDS_MSG_CONFIRM_SAVE    10080
#define IDS_MSG_SAVE_CAP        10081
#define IDS_MSG_FAIL_CREATE_DEV 10082
#define IDS_MSG_FAIL_LOAD_DEV   10083
#define IDS_MSG_FAIL_SAVE_DEV   10084
#define IDS_MSG_DISCONN_PROPS   10085
#define IDS_MSG_NO_LAST_PORT    10086
#define IDS_MSG_PORT_NOT_AVAIL  10087
#define IDS_MSG_DISCONN_IMPORT  10088
#define IDS_MSG_FAIL_OPEN_FILE  10089
#define IDS_MSG_FLASH_MISMATCH  10090
#define IDS_MSG_FAIL_READ_FILE  10091
#define IDS_MSG_FAIL_ALLOC_SNAP 10092
#define IDS_MSG_FAIL_CREATE_FILE 10093
#define IDS_MSG_FAIL_WRITE_FILE 10094
#define IDS_MSG_FAIL_ALLOC      10095
#define IDS_MSG_FAIL_ALLOC_EFUSE 10096
#define IDS_MSG_FAIL_ALLOC_FLASH 10097
#define IDS_MSG_FAIL_DUMP_THREAD 10098
#define IDS_MSG_FAIL_DUMP        10099
#define IDS_MSG_FAIL_UPDATE_DEV  10100
#define IDS_TXT_FILTER           10101
#define IDS_OPEN_DEVICE_TITLE    10102
#define IDS_MSG_OPEN_LAST_FILE   10103
#define IDS_MSG_OPEN_FILE        10104
#define IDS_MSG_OPEN_MULTI_FILE  10105
#define IDS_MSG_ALREADY_RUNNING  10106
#define IDS_MSG_FILE_NOT_FOUND   10107
#define IDS_MSG_ONLY_ESP         10108
#define IDS_TITLE_UNTITLED       10109
#define IDS_STATUS_NO_DEVICE     10110

#endif /* RESOURCE_H */
