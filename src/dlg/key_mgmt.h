/*
 * key_mgmt.h - Key Management dialog
 *
 * Allows importing, exporting, and generating flash encryption keys.
 */

#ifndef DLG_KEY_MGMT_H
#define DLG_KEY_MGMT_H

#include <windows.h>

/*
 * KeyMgmtDlgProc - Key Management dialog procedure
 */
INT_PTR CALLBACK KeyMgmtDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                LPARAM lParam);

#endif /* DLG_KEY_MGMT_H */
