/*
 * key_mgmt.c - Key Management dialog implementation
 *
 * Allows importing, exporting, and generating flash encryption keys.
 * Displays all available key blocks based on chip type.
 *
 * Key block layout per chip:
 * - ESP32:     BLOCK1 at eFuse offset 0x38, 32 bytes
 * - ESP32-S2:  KEY0-KEY5 at eFuse offset 0x9C, 32 bytes each
 * - ESP32-S3:  KEY0-KEY5 at eFuse offset 0x9C, 32 bytes each
 * - ESP32-C2:  KEY0 at eFuse offset 0x60, 32 bytes
 * - ESP32-C3:  KEY0-KEY5 at eFuse offset 0x9C, 32 bytes each
 * - ESP32-C6:  KEY0-KEY5 at eFuse offset 0x9C, 32 bytes each
 *
 * Note: ESP8266 does not support flash encryption.
 */

#include "../app_commands.h"
#include "../utils/lang.h"
#include "dlg.h"
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

/* Constants */
#define KEY_SIZE_MAX 32       /* Maximum key size in bytes (256-bit) */
#define KEY_BLOCK_NAME_MAX 16 /* Maximum key block name length */
#define KEY_BLOCK_DESC_MAX 32 /* Maximum key block description length */
#define HEX_STRING_MAX 256    /* Maximum hex string buffer size */
#define TITLE_MAX 128         /* Maximum title buffer size */

/*
 * GetPurposeName - Get human-readable name for KEY_PURPOSE value
 */
static const WCHAR *GetPurposeName(BYTE purpose)
{
    switch (purpose) {
    case FESP_KEY_PURPOSE_USER:
        return L"USER (0)";
    case FESP_KEY_PURPOSE_RESERVED:
        return L"RESERVED (1)";
    case FESP_KEY_PURPOSE_XTS_AES_256_KEY_1:
        return L"XTS-AES-256-1 (2)";
    case FESP_KEY_PURPOSE_XTS_AES_256_KEY_2:
        return L"XTS-AES-256-2 (3)";
    case FESP_KEY_PURPOSE_XTS_AES_128_KEY:
        return L"XTS-AES-128 (4)";
    case FESP_KEY_PURPOSE_HMAC_DOWN_ALL:
        return L"HMAC-DOWN-ALL (5)";
    case FESP_KEY_PURPOSE_HMAC_DOWN_JTAG:
        return L"HMAC-DOWN-JTAG (6)";
    case FESP_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE:
        return L"HMAC-DOWN-SIG (7)";
    case FESP_KEY_PURPOSE_HMAC_UP:
        return L"HMAC-UP (8)";
    case FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST0:
        return L"SEC-BOOT-DIG0 (9)";
    case FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST1:
        return L"SEC-BOOT-DIG1 (10)";
    case FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST2:
        return L"SEC-BOOT-DIG2 (11)";
    default:
        return L"UNKNOWN";
    }
}

/* Key block information for each entry in the list view */
typedef struct {
    const char *name; /* Block name (e.g. "BLOCK1", "KEY0") */
    const char *desc; /* Description (e.g. "Flash Encryption") */
    int efuse_offset; /* Offset in eFuse byte array */
    int key_size;     /* Key size in bytes (32 for all) */
} KEY_BLOCK_INFO;

/* Number of key blocks per chip type */
#define MAX_KEY_BLOCKS 6

/* Chip-specific key block definitions */
static const KEY_BLOCK_INFO key_blocks_esp32[] = {
    {"BLOCK1", "Flash Encryption", 0x38, 32},
    {"BLOCK2", "Secure Boot", 0x58, 32},
    {"BLOCK3", "User Data", 0x78, 32},
};
#define KEY_BLOCKS_ESP32_COUNT 3

static const KEY_BLOCK_INFO key_blocks_esp32s2[] = {
    {"KEY0", "XTS-AES-256-1", 0x9C, 32}, {"KEY1", "XTS-AES-256-2", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},    {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32},   {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32S2_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32s3[] = {
    {"KEY0", "XTS-AES-256-1", 0x9C, 32}, {"KEY1", "XTS-AES-256-2", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},    {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32},   {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32S3_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32c2[] = {
    {"KEY0", "Flash Encryption", 0x60, 32},
};
#define KEY_BLOCKS_ESP32C2_COUNT 1

static const KEY_BLOCK_INFO key_blocks_esp32c3[] = {
    {"KEY0", "XTS-AES-128", 0x9C, 32}, {"KEY1", "User Key 1", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},  {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32}, {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32C3_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32c6[] = {
    {"KEY0", "XTS-AES-128", 0x9C, 32}, {"KEY1", "User Key 1", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},  {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32}, {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32C6_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32c5[] = {
    {"KEY0", "XTS-AES-128", 0x9C, 32}, {"KEY1", "User Key 1", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},  {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32}, {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32C5_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32c61[] = {
    {"KEY0", "XTS-AES-128", 0x9C, 32}, {"KEY1", "User Key 1", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},  {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32}, {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32C61_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32h2[] = {
    {"KEY0", "XTS-AES-128", 0x9C, 32}, {"KEY1", "User Key 1", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},  {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32}, {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32H2_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32p4[] = {
    {"KEY0", "XTS-AES-128", 0x9C, 32}, {"KEY1", "User Key 1", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},  {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32}, {"KEY5", "User Key 5", 0x13C, 32},
};
#define KEY_BLOCKS_ESP32P4_COUNT 6

static const KEY_BLOCK_INFO key_blocks_esp32s31[] = {
    {"KEY0", "XTS-AES-128", 0x9C, 32}, {"KEY1", "User Key 1", 0xBC, 32},
    {"KEY2", "User Key 2", 0xDC, 32},  {"KEY3", "User Key 3", 0xFC, 32},
    {"KEY4", "User Key 4", 0x11C, 32},
};
#define KEY_BLOCKS_ESP32S31_COUNT 5

/*
 * GetKeyBlocks - Get key blocks for current chip type
 *
 * @count: [out] Number of key blocks
 *
 * Returns pointer to key block array, or NULL if no key blocks available.
 */
static const KEY_BLOCK_INFO *GetKeyBlocks(int *count)
{
    switch (g_chip.type) {
    case FESP_CHIP_ESP8266:
        *count = 0;
        return NULL;
    case FESP_CHIP_ESP32:
        *count = KEY_BLOCKS_ESP32_COUNT;
        return key_blocks_esp32;
    case FESP_CHIP_ESP32S2:
        *count = KEY_BLOCKS_ESP32S2_COUNT;
        return key_blocks_esp32s2;
    case FESP_CHIP_ESP32S3:
        *count = KEY_BLOCKS_ESP32S3_COUNT;
        return key_blocks_esp32s3;
    case FESP_CHIP_ESP32C2:
        *count = KEY_BLOCKS_ESP32C2_COUNT;
        return key_blocks_esp32c2;
    case FESP_CHIP_ESP32C3:
        *count = KEY_BLOCKS_ESP32C3_COUNT;
        return key_blocks_esp32c3;
    case FESP_CHIP_ESP32C6:
        *count = KEY_BLOCKS_ESP32C6_COUNT;
        return key_blocks_esp32c6;
    case FESP_CHIP_ESP32C5:
        *count = KEY_BLOCKS_ESP32C5_COUNT;
        return key_blocks_esp32c5;
    case FESP_CHIP_ESP32C61:
        *count = KEY_BLOCKS_ESP32C61_COUNT;
        return key_blocks_esp32c61;
    case FESP_CHIP_ESP32H2:
        *count = KEY_BLOCKS_ESP32H2_COUNT;
        return key_blocks_esp32h2;
    case FESP_CHIP_ESP32P4:
        *count = KEY_BLOCKS_ESP32P4_COUNT;
        return key_blocks_esp32p4;
    case FESP_CHIP_ESP32S31:
        *count = KEY_BLOCKS_ESP32S31_COUNT;
        return key_blocks_esp32s31;
    default:
        *count = 0;
        return NULL;
    }
}

/*
 * IsValidKeyRange - Check if eFuse offset + size is within bounds
 *
 * @chip:   Pointer to chip context
 * @offset: eFuse offset
 * @size:   Key size in bytes
 *
 * Returns TRUE if the range is valid, FALSE otherwise.
 */
static BOOL IsValidKeyRange(const fesp_chip_ctx_t *chip, int offset, int size)
{
    if (!chip) {
        return FALSE;
    }
    if (offset < 0 || size <= 0 || size > KEY_SIZE_MAX) {
        return FALSE;
    }
    const BYTE *efuse = fesp_chip_get_efuse(chip);
    if (!efuse) {
        return FALSE;
    }
    int efuse_size = fesp_chip_get_efuse_size(chip);
    if (efuse_size <= 0) {
        return FALSE;
    }
    if (offset + size > efuse_size) {
        return FALSE;
    }
    return TRUE;
}

/*
 * IsKeyEmpty - Check if key block data is all zeros (empty)
 *
 * @chip:   Pointer to chip context
 * @offset: eFuse offset
 * @size:   Key size in bytes
 *
 * Returns TRUE if key is empty or invalid range, FALSE if key is set.
 */
static BOOL IsKeyEmpty(const fesp_chip_ctx_t *chip, int offset, int size)
{
    if (!IsValidKeyRange(chip, offset, size)) {
        return TRUE;
    }
    const BYTE *efuse = fesp_chip_get_efuse(chip);
    for (int i = 0; i < size; i++) {
        if (efuse[offset + i] != 0x00) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * FormatKeyHex - Format key data as hex string
 *
 * @chip:     Pointer to chip context
 * @offset:   eFuse offset
 * @size:     Key size in bytes
 * @buf:      Output buffer
 * @bufChars: Buffer size in characters
 *
 * Formats key as "01 02 03 ..." hex string.
 */
static void FormatKeyHex(const fesp_chip_ctx_t *chip, int offset, int size,
                         WCHAR *buf, int bufChars)
{
    if (!IsValidKeyRange(chip, offset, size) || !buf || bufChars < 4) {
        if (buf && bufChars > 0) {
            buf[0] = L'\0';
        }
        return;
    }

    const BYTE *efuse = fesp_chip_get_efuse(chip);
    WCHAR *p = buf;
    WCHAR *end = buf + bufChars;

    for (int i = 0; i < size && p < end - 4; i++) {
        wsprintfW(p, L"%02X", efuse[offset + i]);
        p += 2;
        if (i < size - 1 && p < end - 1) {
            *p++ = L' ';
        }
    }
    *p = L'\0';
}

/*
 * ReadKey - Read key data from eFuse into buffer
 *
 * @chip:   Pointer to chip context
 * @offset: eFuse offset
 * @size:   Key size in bytes
 * @key:    Output buffer
 *
 * Returns TRUE on success, FALSE on invalid range.
 */
static BOOL ReadKey(const fesp_chip_ctx_t *chip, int offset, int size,
                    BYTE *key)
{
    if (!key) {
        return FALSE;
    }
    if (!IsValidKeyRange(chip, offset, size)) {
        memset(key, 0, size);
        return FALSE;
    }
    const BYTE *efuse = fesp_chip_get_efuse(chip);
    memcpy(key, efuse + offset, size);
    return TRUE;
}

/*
 * WriteKey - Write key data from buffer into eFuse
 *
 * @chip:   Pointer to chip context
 * @offset: eFuse offset
 * @size:   Key size in bytes
 * @key:    Key data to write
 *
 * Returns TRUE on success, FALSE on invalid range.
 */
static BOOL WriteKey(fesp_chip_ctx_t *chip, int offset, int size,
                     const BYTE *key)
{
    if (!key) {
        return FALSE;
    }
    if (!IsValidKeyRange(chip, offset, size)) {
        return FALSE;
    }
    BYTE *efuse = fesp_chip_get_efuse_mut(chip);
    if (!efuse) {
        return FALSE;
    }
    memcpy(efuse + offset, key, size);
    return TRUE;
}

/*
 * GenerateRandomKey - Generate random key data
 *
 * @key:  Output buffer
 * @size: Key size in bytes
 *
 * Uses Windows CryptoAPI for secure random generation.
 * Falls back to rand() if CryptoAPI is unavailable.
 */
static void GenerateRandomKey(BYTE *key, int size)
{
    if (!key || size <= 0 || size > KEY_SIZE_MAX) {
        return;
    }

    HCRYPTPROV hProv = 0;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL,
                            CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProv, size, key);
        CryptReleaseContext(hProv, 0);
    } else {
        /* Fallback: simple pseudo-random */
        for (int i = 0; i < size; i++) {
            key[i] = (BYTE)(rand() & 0xFF);
        }
    }
}

/*
 * GetSelectedKeyBlock - Get the selected key block info
 *
 * @hList:  Handle to ListView control
 * @blocks: [out] Pointer to key block array
 *
 * Returns selected index, or -1 if no selection.
 */
static int GetSelectedKeyBlock(HWND hList, const KEY_BLOCK_INFO **blocks)
{
    int sel = (int)SendMessageW(hList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (sel < 0) {
        return -1;
    }

    int count;
    const KEY_BLOCK_INFO *info = GetKeyBlocks(&count);
    if (!info || sel >= count) {
        return -1;
    }

    *blocks = info;
    return sel;
}

/*
 * RefreshListView - Refresh the ListView with current key block data
 *
 * @hList: Handle to ListView control
 *
 * Updates all items and button states.
 *
 * @hList:       Handle to ListView control
 * @selectIndex: Index of item to select (-1 for no selection)
 */
static void RefreshListView(HWND hList, int selectIndex)
{
    ListView_DeleteAllItems(hList);

    int count;
    const KEY_BLOCK_INFO *blocks = GetKeyBlocks(&count);

    for (int i = 0; i < count; i++) {
        LVITEMW item = {0};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.lParam = i;

        /* Column 0: Block name */
        WCHAR wName[KEY_BLOCK_NAME_MAX];
        MultiByteToWideChar(CP_UTF8, 0, blocks[i].name, -1, wName,
                            KEY_BLOCK_NAME_MAX);
        item.pszText = wName;
        int idx = ListView_InsertItem(hList, &item);

        /* Column 1: Purpose (from eFuse KEY_PURPOSE field) */
        BYTE purpose = fesp_efuse_get_key_purpose(&g_chip, i);
        ListView_SetItemText(hList, idx, 1, (LPWSTR)GetPurposeName(purpose));

        /* Column 2: Status (set/empty) */
        BOOL empty =
            IsKeyEmpty(&g_chip, blocks[i].efuse_offset, blocks[i].key_size);
        ListView_SetItemText(hList, idx, 2,
                             (LPWSTR)LoadStr(empty ? IDS_KEY_MGMT_STATUS_EMPTY
                                                   : IDS_KEY_MGMT_STATUS_SET));

        /* Column 3: Size */
        WCHAR wSize[16];
        wsprintfW(wSize, L"%d bytes", blocks[i].key_size);
        ListView_SetItemText(hList, idx, 3, wSize);
    }

    /* Select item */
    if (selectIndex >= 0 && selectIndex < count) {
        ListView_SetItemState(hList, selectIndex, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(hList, selectIndex, FALSE);
    }

    /* Update button states */
    BOOL connected = Serial_IsOpen(&g_serial);
    HWND hDlg = GetParent(hList);
    HWND hImport = GetDlgItem(hDlg, IDC_KEY_IMPORT);
    HWND hExport = GetDlgItem(hDlg, IDC_KEY_EXPORT);
    HWND hGenerate = GetDlgItem(hDlg, IDC_KEY_GENERATE);
    HWND hClear = GetDlgItem(hDlg, IDC_KEY_CLEAR);
    HWND hPurpose = GetDlgItem(hDlg, IDC_KEY_PURPOSE);

    if (hImport) {
        EnableWindow(hImport, !connected && count > 0);
    }
    if (hExport) {
        EnableWindow(hExport, !connected && count > 0);
    }
    if (hGenerate) {
        EnableWindow(hGenerate, !connected && count > 0);
    }
    if (hClear) {
        EnableWindow(hClear, !connected && count > 0);
    }

    /* Purpose button: enabled only for S2/S3/C3/C6 (not ESP32/C2/ESP8266) */
    if (hPurpose) {
        BOOL canChange =
            !connected && count > 0 && g_chip.type != FESP_CHIP_ESP8266 &&
            g_chip.type != FESP_CHIP_ESP32 && g_chip.type != FESP_CHIP_ESP32C2;
        EnableWindow(hPurpose, canChange);
    }
}

/*
 * HandleImport - Handle Import button click
 *
 * @hDlg: Handle to dialog
 * @hList: Handle to ListView control
 *
 * Imports key from .bin file to selected key block.
 */
static void HandleImport(HWND hDlg, HWND hList)
{
    const KEY_BLOCK_INFO *blocks = NULL;
    int sel = GetSelectedKeyBlock(hList, &blocks);
    if (sel < 0) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_SELECT_BLOCK),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONWARNING);
        return;
    }

    /* Open file dialog */
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg;
    ofn.lpstrFilter = LoadStr(IDS_KEY_MGMT_FILE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_IMPORT_KEY);

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    /* Read file */
    HANDLE hFile = CreateFileW(szFile, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_FAIL_OPEN),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize != (DWORD)blocks[sel].key_size) {
        WCHAR msg[128];
        wsprintfW(msg, LoadStr(IDS_KEY_MGMT_SIZE_MISMATCH), fileSize,
                  blocks[sel].key_size);
        MessageBoxW(hDlg, msg, LoadStr(IDS_KEY_MGMT_CAPTION),
                    MB_OK | MB_ICONERROR);
        CloseHandle(hFile);
        return;
    }

    BYTE key[KEY_SIZE_MAX] = {0};
    DWORD bytesRead;
    BOOL ok =
        ReadFile(hFile, key, (DWORD)blocks[sel].key_size, &bytesRead, NULL) &&
        bytesRead == (DWORD)blocks[sel].key_size;
    CloseHandle(hFile);

    if (!ok) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_FAIL_READ),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONERROR);
        return;
    }

    /* Write key to eFuse */
    WriteKey(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size, key);
    g_deviceModified = TRUE;

    /* Refresh list and keep selection */
    RefreshListView(hList, sel);

    /* Update hex display */
    WCHAR hexStr[HEX_STRING_MAX] = {0};
    FormatKeyHex(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size,
                 hexStr, HEX_STRING_MAX);
    SetDlgItemTextW(hDlg, IDC_KEY_HEX, hexStr);
}

/*
 * HandleExport - Handle Export button click
 *
 * @hDlg: Handle to dialog
 * @hList: Handle to ListView control
 *
 * Exports key from selected key block to .bin file.
 */
static void HandleExport(HWND hDlg, HWND hList)
{
    const KEY_BLOCK_INFO *blocks = NULL;
    int sel = GetSelectedKeyBlock(hList, &blocks);
    if (sel < 0) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_SELECT_BLOCK),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONWARNING);
        return;
    }

    if (IsKeyEmpty(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size)) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_KEY_EMPTY),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONWARNING);
        return;
    }

    /* Save file dialog */
    OPENFILENAMEW ofn = {0};
    WCHAR szFile[MAX_PATH] = {0};
    wsprintfW(szFile, L"%hs.bin", blocks[sel].name);
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg;
    ofn.lpstrFilter = LoadStr(IDS_KEY_MGMT_FILE_FILTER);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"bin";
    ofn.lpstrTitle = LoadStr(IDS_DLG_TITLE_EXPORT_KEY);

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    /* Read key from eFuse */
    BYTE key[KEY_SIZE_MAX] = {0};
    ReadKey(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size, key);

    /* Write file */
    HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_FAIL_CREATE),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONERROR);
        return;
    }

    DWORD bytesWritten;
    BOOL ok = WriteFile(hFile, key, (DWORD)blocks[sel].key_size, &bytesWritten,
                        NULL) &&
              bytesWritten == (DWORD)blocks[sel].key_size;
    CloseHandle(hFile);

    if (!ok) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_FAIL_WRITE),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONERROR);
        return;
    }
}

/*
 * HandleGenerate - Handle Generate button click
 *
 * @hDlg: Handle to dialog
 * @hList: Handle to ListView control
 *
 * Generates random key and writes to selected key block.
 */
static void HandleGenerate(HWND hDlg, HWND hList)
{
    const KEY_BLOCK_INFO *blocks = NULL;
    int sel = GetSelectedKeyBlock(hList, &blocks);
    if (sel < 0) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_SELECT_BLOCK),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONWARNING);
        return;
    }

    /* Confirm overwrite if key is not empty */
    if (!IsKeyEmpty(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size)) {
        int ret = MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_CONFIRM_OVERWRITE),
                              LoadStr(IDS_KEY_MGMT_CAPTION),
                              MB_YESNO | MB_ICONQUESTION);
        if (ret != IDYES) {
            return;
        }
    }

    /* Generate random key */
    BYTE key[KEY_SIZE_MAX] = {0};
    GenerateRandomKey(key, blocks[sel].key_size);

    /* Write key to eFuse */
    WriteKey(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size, key);
    g_deviceModified = TRUE;

    /* Refresh list and keep selection */
    RefreshListView(hList, sel);

    /* Update hex display */
    WCHAR hexStr[HEX_STRING_MAX] = {0};
    FormatKeyHex(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size,
                 hexStr, HEX_STRING_MAX);
    SetDlgItemTextW(hDlg, IDC_KEY_HEX, hexStr);
}

/*
 * HandleClear - Handle Clear button click
 *
 * @hDlg: Handle to dialog
 * @hList: Handle to ListView control
 *
 * Clears selected key block (writes zeros).
 */
static void HandleClear(HWND hDlg, HWND hList)
{
    const KEY_BLOCK_INFO *blocks = NULL;
    int sel = GetSelectedKeyBlock(hList, &blocks);
    if (sel < 0) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_SELECT_BLOCK),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONWARNING);
        return;
    }

    if (IsKeyEmpty(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size)) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_ALREADY_EMPTY),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONINFORMATION);
        return;
    }

    /* Confirm clear */
    int ret =
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_CONFIRM_CLEAR),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_YESNO | MB_ICONWARNING);
    if (ret != IDYES) {
        return;
    }

    /* Clear key (write zeros) */
    BYTE key[KEY_SIZE_MAX] = {0};
    WriteKey(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size, key);
    g_deviceModified = TRUE;

    /* Refresh list and keep selection */
    RefreshListView(hList, sel);

    /* Clear hex display */
    SetDlgItemTextW(hDlg, IDC_KEY_HEX, L"");
}

/*
 * HandlePurpose - Handle Purpose button click
 *
 * Shows a dialog to change the KEY_PURPOSE of the selected key block.
 * Only available for S2/S3/C3/C6 chips.
 */
static void HandlePurpose(HWND hDlg, HWND hList)
{
    const KEY_BLOCK_INFO *blocks = NULL;
    int sel = GetSelectedKeyBlock(hList, &blocks);
    if (sel < 0) {
        MessageBoxW(hDlg, LoadStr(IDS_KEY_MGMT_SELECT_BLOCK),
                    LoadStr(IDS_KEY_MGMT_CAPTION), MB_OK | MB_ICONWARNING);
        return;
    }

    /* Build purpose list */
    BYTE currentPurpose = fesp_efuse_get_key_purpose(&g_chip, sel);
    BOOL isS3Key5 = ((g_chip.type == FESP_CHIP_ESP32S3 ||
                      g_chip.type == FESP_CHIP_ESP32C3 ||
                      g_chip.type == FESP_CHIP_ESP32C6) &&
                     sel == 5);

    /* Simple dialog using MessageBox with choices isn't ideal;
       use a combo box in a dialog. For simplicity, use a track popup menu. */
    HMENU hMenu = CreatePopupMenu();
    const BYTE purposes[] = {
        FESP_KEY_PURPOSE_USER,
        FESP_KEY_PURPOSE_XTS_AES_128_KEY,
        FESP_KEY_PURPOSE_XTS_AES_256_KEY_1,
        FESP_KEY_PURPOSE_XTS_AES_256_KEY_2,
        FESP_KEY_PURPOSE_HMAC_DOWN_ALL,
        FESP_KEY_PURPOSE_HMAC_DOWN_JTAG,
        FESP_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE,
        FESP_KEY_PURPOSE_HMAC_UP,
        FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST0,
        FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST1,
        FESP_KEY_PURPOSE_SECURE_BOOT_DIGEST2,
    };
    for (int i = 0; i < (int)(sizeof(purposes) / sizeof(purposes[0])); i++) {
        BYTE p = purposes[i];
        UINT flags = MF_STRING;
        if (p == currentPurpose) {
            flags |= MF_CHECKED;
        }
        /* ESP32-S3/C3/C6 KEY5: disable XTS_AES purposes */
        if (isS3Key5 && (p == FESP_KEY_PURPOSE_XTS_AES_128_KEY ||
                         p == FESP_KEY_PURPOSE_XTS_AES_256_KEY_1 ||
                         p == FESP_KEY_PURPOSE_XTS_AES_256_KEY_2)) {
            flags |= MF_DISABLED;
        }
        AppendMenuW(hMenu, flags, (UINT_PTR)(p + 1), GetPurposeName(p));
    }

    /* Show popup menu near the button */
    HWND hBtn = GetDlgItem(hDlg, IDC_KEY_PURPOSE);
    RECT rc;
    GetWindowRect(hBtn, &rc);
    int chosen = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, rc.left,
                                rc.bottom, 0, hDlg, NULL);
    DestroyMenu(hMenu);

    if (chosen == 0) {
        return;
    }

    BYTE newPurpose = (BYTE)(chosen - 1);
    if (newPurpose == currentPurpose) {
        return;
    }

    /* Set new purpose */
    fesp_efuse_set_key_purpose(&g_chip, sel, newPurpose);
    g_deviceModified = TRUE;

    /* Refresh list and keep selection */
    RefreshListView(hList, sel);
}

/*
 * HandleInitDialog - Handle WM_INITDIALOG message
 *
 * @hDlg: Handle to dialog
 *
 * Initializes dialog controls and populates key block list.
 */
static void HandleInitDialog(HWND hDlg)
{
    /* Center dialog on parent */
    HWND hParent = GetParent(hDlg);
    if (hParent) {
        RECT rcParent, rcDlg;
        GetWindowRect(hParent, &rcParent);
        GetWindowRect(hDlg, &rcDlg);
        int x =
            (rcParent.left + rcParent.right - (rcDlg.right - rcDlg.left)) / 2;
        int y =
            (rcParent.top + rcParent.bottom - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    /* Set dialog title with chip name */
    WCHAR title[TITLE_MAX];
    WCHAR chipName[32];
    MultiByteToWideChar(CP_UTF8, 0, g_chip.name, -1, chipName, 32);
    wsprintfW(title, LoadStr(IDS_KEY_MGMT_TITLE), chipName);
    SetWindowTextW(hDlg, title);

    /* Initialize ListView */
    HWND hList = GetDlgItem(hDlg, IDC_KEY_LIST);
    ListView_SetExtendedListViewStyle(hList,
                                      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    /* Add columns */
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;

    col.cx = 70;
    col.pszText = (LPWSTR)LoadStr(IDS_KEY_MGMT_COLUMN_BLOCK);
    ListView_InsertColumn(hList, 0, &col);
    col.cx = 140;
    col.pszText = (LPWSTR)LoadStr(IDS_KEY_MGMT_COLUMN_PURPOSE);
    ListView_InsertColumn(hList, 1, &col);
    col.cx = 60;
    col.pszText = (LPWSTR)LoadStr(IDS_KEY_MGMT_COLUMN_STATUS);
    ListView_InsertColumn(hList, 2, &col);
    col.cx = 70;
    col.pszText = (LPWSTR)LoadStr(IDS_KEY_MGMT_COLUMN_SIZE);
    ListView_InsertColumn(hList, 3, &col);

    /* Populate list and select first item */
    RefreshListView(hList, 0);
}

/*
 * HandleItemChanged - Handle LVN_ITEMCHANGED notification
 *
 * @hDlg:   Handle to dialog
 * @nmlv:   Pointer to NMLISTVIEW structure
 *
 * Updates hex display when selection changes.
 */
static void HandleItemChanged(HWND hDlg, const NMLISTVIEW *nmlv)
{
    if (!(nmlv->uNewState & LVIS_SELECTED)) {
        return;
    }

    int sel = nmlv->iItem;

    int count;
    const KEY_BLOCK_INFO *blocks = GetKeyBlocks(&count);

    /* Update key hex display */
    WCHAR hexStr[HEX_STRING_MAX] = {0};
    if (blocks && sel >= 0 && sel < count) {
        FormatKeyHex(&g_chip, blocks[sel].efuse_offset, blocks[sel].key_size,
                     hexStr, HEX_STRING_MAX);
    }
    SetDlgItemTextW(hDlg, IDC_KEY_HEX, hexStr);
}

/*
 * KeyMgmtDlgProc - Key Management dialog procedure
 */
INT_PTR CALLBACK KeyMgmtDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        HandleInitDialog(hDlg);
        return TRUE;

    case WM_COMMAND: {
        HWND hList = GetDlgItem(hDlg, IDC_KEY_LIST);
        switch (LOWORD(wParam)) {
        case IDC_KEY_IMPORT:
            HandleImport(hDlg, hList);
            return TRUE;

        case IDC_KEY_EXPORT:
            HandleExport(hDlg, hList);
            return TRUE;

        case IDC_KEY_GENERATE:
            HandleGenerate(hDlg, hList);
            return TRUE;

        case IDC_KEY_CLEAR:
            HandleClear(hDlg, hList);
            return TRUE;

        case IDC_KEY_PURPOSE:
            HandlePurpose(hDlg, hList);
            return TRUE;

        case IDC_KEY_CLOSE:
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
    } break;

    case WM_NOTIFY: {
        NMHDR *nmhdr = (NMHDR *)lParam;
        if (nmhdr->idFrom == IDC_KEY_LIST && nmhdr->code == LVN_ITEMCHANGED) {
            HandleItemChanged(hDlg, (NMLISTVIEW *)lParam);
        }
    } break;
    }
    return FALSE;
}
