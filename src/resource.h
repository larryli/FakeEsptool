/*
 * resource.h - Resource ID definitions
 */

#ifndef RESOURCE_H
#define RESOURCE_H

/* Main window control IDs */
#define IDC_MAIN_EDIT 1001
#define IDC_MAIN_TOOLBAR 1002
#define IDC_MAIN_STATUSBAR 1003

/* Menu command IDs */
#define IDM_NEW_DEVICE 2000
#define IDM_OPEN_DEVICE 2001
#define IDM_SAVE_DEVICE 2002
#define IDM_SAVE_DEVICE_AS 2003
#define IDM_DEVICE_PROPS 2004
#define IDM_CONNECT 2005
#define IDM_DISCONNECT 2006
#define IDM_RECONNECT 2007
#define IDM_FLASH_IMPORT 2008
#define IDM_FLASH_EXPORT 2009
#define IDM_DUMP_DEVICE_AS 2010
#define IDM_LOG_CLEAR 2011
#define IDM_LOG_SAVEAS 2012
#define IDM_LOG_FONT 2013
#define IDM_EXIT 2014
#define IDM_ABOUT 2015
#define IDM_KEY_MGMT 2016
#define IDM_EFUSE_IMPORT 2017
#define IDM_EFUSE_EXPORT 2018
#define IDR_MNU_FLASH_IMPORT 2019
#define IDR_MNU_FLASH_EXPORT 2020

/* Encryption state menu IDs */
#define IDM_ENCRYPT_NONE 2020
#define IDM_ENCRYPT_DEV 2021
#define IDM_ENCRYPT_RELEASE 2022

/* Download mode menu IDs */
#define IDM_DOWNLOAD_NORMAL 2030
#define IDM_DOWNLOAD_SECURE 2031
#define IDM_DOWNLOAD_DISABLED 2032

/* Dialog IDs */
#define IDD_PORT_SELECT 3001
#define IDC_PORT_COMBO 3002
#define IDD_ABOUT 3003
#define IDD_DEVICE_PROPS 3004
#define IDD_KEY_MGMT 3005

/* About dialog controls */
#define IDD_APPNAME 3101
#define IDD_COPYRIGHT 3102
#define IDD_WEBSITE 3103

/* Device Properties dialog controls */
#define IDC_CHIP_COMBO 3201
#define IDC_FLASH_SIZE_COMBO 3202
#define IDC_MAC_EDIT 3203
#define IDC_RANDOM_MAC 3204
#define IDC_XTAL_FREQ_COMBO 3205

/* Key Management dialog controls */
#define IDC_KEY_LIST 3301
#define IDC_KEY_HEX 3302
#define IDC_KEY_IMPORT 3303
#define IDC_KEY_EXPORT 3304
#define IDC_KEY_GENERATE 3305
#define IDC_KEY_CLEAR 3306
#define IDC_KEY_PURPOSE 3307
#define IDC_KEY_CLOSE 3308

/* Icon and menu resource IDs */
#define IDI_APP 5001
#define IDR_MAIN_MENU 6001
#define IDR_MAIN_ACCEL 6002

/* Toolbar bitmap resources */
#define IDB_TOOLBAR 7001

/* String IDs */
#define IDS_APP_NAME 10001
#define IDS_DISCONNECTED 10002
#define IDS_TITLE_FORMAT 10003 /* "FakeEsptool - %s" */

/* Tooltip strings */
#define IDS_TIP_NEW_DEVICE 10031
#define IDS_TIP_OPEN_DEVICE 10032
#define IDS_TIP_SAVE_DEVICE 10033
#define IDS_TIP_DEVPROPS 10034
#define IDS_TIP_CONNECT 10035
#define IDS_TIP_DISCONNECT 10036
#define IDS_TIP_RECONNECT 10037
#define IDS_TIP_FLASH_IMPORT 10038
#define IDS_TIP_FLASH_EXPORT 10039
#define IDS_TIP_KEY_MGMT 10040
#define IDS_TIP_CLEAR 10041
#define IDS_TIP_SAVEAS 10042

/* Message strings */
#define IDS_MSG_CONN_TITLE 10061
#define IDS_MSG_PORT_ERROR 10064
#define IDS_MSG_ERROR 10065
#define IDS_MSG_SELECT_PORT 10066
#define IDS_MSG_WARNING 10067
#define IDS_MSG_INVALID_PORT 10068
#define IDS_MSG_SAVE_ERROR 10069
#define IDS_MSG_DEV_REMOVED 10070
#define IDS_MSG_DEV_TITLE 10071
#define IDS_LOG_SAVE_FILTER 10072
#define IDS_MSG_CONN_LOST 10074
#define IDS_DEVICE_FILTER 10075
#define IDS_BIN_FILTER 10076
#define IDS_FLASH_SIZE_MISMATCH 10077
#define IDS_MSG_CONFIRM_DISCONN 10078
#define IDS_MSG_DISCONN_CAP 10079
#define IDS_MSG_CONFIRM_SAVE 10080
#define IDS_MSG_SAVE_CAP 10081
#define IDS_MSG_FAIL_CREATE_DEV 10082
#define IDS_MSG_FAIL_LOAD_DEV 10083
#define IDS_MSG_FAIL_SAVE_DEV 10084
#define IDS_MSG_DISCONN_PROPS 10085
#define IDS_MSG_NO_LAST_PORT 10086
#define IDS_MSG_PORT_NOT_AVAIL 10087
#define IDS_MSG_DISCONN_IMPORT 10088
#define IDS_MSG_FAIL_OPEN_FILE 10089
#define IDS_MSG_FLASH_MISMATCH 10090
#define IDS_MSG_FAIL_READ_FILE 10091
#define IDS_MSG_FAIL_ALLOC_SNAP 10092
#define IDS_MSG_FAIL_CREATE_FILE 10093
#define IDS_MSG_FAIL_WRITE_FILE 10094
#define IDS_MSG_FAIL_ALLOC 10095
#define IDS_MSG_FAIL_ALLOC_EFUSE 10096
#define IDS_MSG_FAIL_ALLOC_FLASH 10097
#define IDS_MSG_FAIL_DUMP_THREAD 10098
#define IDS_MSG_FAIL_DUMP 10099
#define IDS_MSG_FAIL_UPDATE_DEV 10100
#define IDS_TXT_FILTER 10101
#define IDS_OPEN_DEVICE_TITLE 10102
#define IDS_MSG_OPEN_LAST_FILE 10103
#define IDS_MSG_OPEN_FILE 10104
#define IDS_MSG_OPEN_MULTI_FILE 10105
#define IDS_MSG_ALREADY_RUNNING 10106
#define IDS_MSG_FILE_NOT_FOUND 10107
#define IDS_MSG_ONLY_ESP 10108
#define IDS_TITLE_UNTITLED 10109
#define IDS_STATUS_NO_DEVICE 10110

/* eFuse import/export message strings */
#define IDS_MSG_EFUSE_SIZE_MISMATCH 10250

/* File dialog title strings */
#define IDS_DLG_TITLE_SAVE_DEVICE 10260
#define IDS_DLG_TITLE_OPEN_DEVICE 10261
#define IDS_DLG_TITLE_SAVE_DEVICE_AS 10262
#define IDS_DLG_TITLE_IMPORT_FLASH 10263
#define IDS_DLG_TITLE_EXPORT_FLASH 10264
#define IDS_DLG_TITLE_IMPORT_EFUSE 10265
#define IDS_DLG_TITLE_EXPORT_EFUSE 10266
#define IDS_DLG_TITLE_DUMP_DEVICE 10267
#define IDS_DLG_TITLE_SAVE_LOG 10268
#define IDS_DLG_TITLE_IMPORT_KEY 10269
#define IDS_DLG_TITLE_EXPORT_KEY 10270

/* Encryption status strings */
#define IDS_ENCRYPT_NONE 10120    /* "No Encryption" */
#define IDS_ENCRYPT_DEV 10121     /* "Encrypted (Dev)" */
#define IDS_ENCRYPT_RELEASE 10122 /* "Encrypted (Release)" */

/* Download mode status strings */
#define IDS_DOWNLOAD_NORMAL 10130   /* "Download Normal" */
#define IDS_DOWNLOAD_SECURE 10131   /* "Download Secure" */
#define IDS_DOWNLOAD_DISABLED 10132 /* "Download Disabled" */

/* Status bar strings */
#define IDS_SB_SECURE_BOOT_ENABLED 10140  /* "Secure Boot" */
#define IDS_SB_SECURE_BOOT_DISABLED 10141 /* "No Secure Boot" */
#define IDS_SB_JTAG_DISABLED 10142        /* "JTAG Disabled" */
#define IDS_SB_JTAG_ENABLED 10143         /* "JTAG Enabled" */
#define IDS_SB_JTAG_PARTIAL 10144         /* "JTAG Partial" */

/* Status bar tooltip format strings */
#define IDS_TIP_XTAL_MAC 10150    /* "%hs AA:BB:CC:DD:EE:FF" */
#define IDS_TIP_EFUSE_FIELD 10151 /* "%hs = 0x%02X" */

/* Key Management dialog strings */
#define IDS_KEY_MGMT_TITLE 10200          /* "Key Management - %s" */
#define IDS_KEY_MGMT_COLUMN_BLOCK 10201   /* "Block" */
#define IDS_KEY_MGMT_COLUMN_PURPOSE 10202 /* "Purpose" */
#define IDS_KEY_MGMT_COLUMN_STATUS 10203  /* "Status" */
#define IDS_KEY_MGMT_COLUMN_SIZE 10204    /* "Size" */
#define IDS_KEY_MGMT_STATUS_EMPTY 10205   /* "Empty" */
#define IDS_KEY_MGMT_STATUS_SET 10206     /* "Set" */
#define IDS_KEY_MGMT_SELECT_BLOCK 10207   /* "Please select a key block." */
#define IDS_KEY_MGMT_FILE_FILTER                                               \
    10208 /* "Binary Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0" */
#define IDS_KEY_MGMT_FAIL_OPEN 10209   /* "Failed to open file." */
#define IDS_KEY_MGMT_FAIL_READ 10210   /* "Failed to read key file." */
#define IDS_KEY_MGMT_FAIL_CREATE 10211 /* "Failed to create file." */
#define IDS_KEY_MGMT_FAIL_WRITE 10212  /* "Failed to write key file." */
#define IDS_KEY_MGMT_SIZE_MISMATCH                                             \
    10213 /* "Key file size (%lu bytes) does not match key block size (%d      \
             bytes)." */
#define IDS_KEY_MGMT_KEY_EMPTY 10214 /* "Selected key block is empty." */
#define IDS_KEY_MGMT_CONFIRM_OVERWRITE                                         \
    10215 /* "Selected key block already contains\na key. Overwrite?" */
#define IDS_KEY_MGMT_CONFIRM_CLEAR                                             \
    10216 /* "Clear the selected key block? This cannot be undone." */
#define IDS_KEY_MGMT_ALREADY_EMPTY                                             \
    10217                          /* "Selected key block is already empty." */
#define IDS_KEY_MGMT_CAPTION 10219 /* "Key Management" */

#endif /* RESOURCE_H */
