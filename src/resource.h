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
#define IDM_CONNECT         2001
#define IDM_DISCONNECT      2002
#define IDM_PING            2003
#define IDM_LOG_CLEAR       2004
#define IDM_LOG_SAVEAS      2005
#define IDM_LOG_FONT        2006
#define IDM_EXIT            2007
#define IDM_ABOUT           2008

/* Dialog IDs */
#define IDD_PORT_SELECT     3001
#define IDC_PORT_COMBO      3002
#define IDD_ABOUT           3003

/* Icon and menu resource IDs */
#define IDI_APP             5001
#define IDR_MAIN_MENU       6001

/* Toolbar bitmap resources */
#define IDB_TOOLBAR         7001

/* String IDs */
#define IDS_APP_NAME        10001
#define IDS_DISCONNECTED    10002
#define IDS_TITLE_FORMAT    10004  /* "FakeEsptool - %s" */

/* Tooltip strings */
#define IDS_TIP_CONNECT     10030
#define IDS_TIP_DISCONNECT  10031
#define IDS_TIP_PING        10032
#define IDS_TIP_CLEAR       10033
#define IDS_TIP_SAVEAS      10034

/* Message strings */
#define IDS_MSG_NOT_CONN    10060
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

#endif /* RESOURCE_H */
