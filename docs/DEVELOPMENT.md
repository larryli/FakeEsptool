# FakeEsptool 开发文档

## 项目概述

FakeEsptool 是一个 ESP 芯片设备端模拟器，用于模拟 ESP8266/ESP32 系列芯片响应 esptool 客户端的烧录协议。

## 架构概览

```
┌──────────────────────────────────────────────┐
│  GUI 层                                       │
│  main.c  app_commands.c  dlg/*.c  serial.c   │
│  device_file.c（.esp 文件格式）               │
└──────────┬───────────────────────────────────┘
           │ 注册 HAL 回调
           ▼
┌──────────────────────────────────────────────┐
│  esptool_hal.h/c  — 平台合同（HAL）           │
│  输出回调：Write / SetBaudRate / Modified     │
│  日志：LogI / LogE（GUI） / LogD（trace 文件） │
│  工具：Mem_* / MD5 / Deflate / Encrypt       │
└──────────┬───────────────────────────────────┘
           │
           ▼
┌──────────────────────────────────────────────┐
│  模拟引擎（src/fesptool/）— 平台无关          │
│  esptool.c/h  协议命令解析与响应              │
│  slip.c/h     SLIP 编解码                    │
│  chip.c/h     芯片模拟骨架                    │
│  efuse.c/h    eFuse 模拟                     │
│  flash.c/h    Flash 存储模拟                  │
└──────────────────────────────────────────────┘
```

### 数据共享机制

模拟引擎通过指针直接引用芯片和 Flash 数据，无需复制：

```
g_chip  ◄─── g_esptool.chip   (同一块内存)
g_flash ◄─── g_esptool.flash  (同一块内存)
```

**初始化**：
```c
fesp_chip_init(&g_chip, FESP_CHIP_ESP32);
fesp_flash_init(&g_flash, 4 * 1024 * 1024);
fesp_init(&g_esptool, &g_chip, &g_flash);
```

### 命名规范

模拟引擎使用 `fesp_` 前缀（FakeEsptool 缩写）：

| 类别 | 命名模式 | 示例 |
|---|---|---|
| 函数 | `fesp_模块_动作` | `fesp_chip_init`、`fesp_efuse_get_key_purpose` |
| 结构体 | `fesp_模块_ctx_t` | `fesp_chip_ctx_t`、`fesp_flash_ctx_t` |
| 枚举 | `FESP_CHIP_名称` | `FESP_CHIP_ESP32`、`FESP_CHIP_ESP32S3` |
| 常量 | `FESP_类别_名称` | `FESP_OK`、`FESP_CHIP_DETECT_REG` |

### 头文件结构

| 文件 | 可见性 | 内容 |
|---|---|---|
| `fesp.h` | 公开 | 聚合器，`#include` 全部公共头 |
| `chip.h` | 公开 | 芯片类型、结构体、API 函数 |
| `chip_priv.h` | 内部 | 寄存器地址、偏移量、内部函数 |
| `efuse.h` | 公开 | eFuse 查询/设置 API、常量 |
| `efuse_priv.h` | 内部 | eFuse 控制器寄存器、位偏移常量 |
| `flash.h` | 公开 | Flash 结构体、API 函数 |
| `slip.h` | 公开 | SLIP 结构体、API 函数 |
| `esptool.h` | 公开 | 协议结构体、API 函数、命令码 |
| `esptool_priv.h` | 内部 | 协议内部声明 |

外部使用：`#include "fesptool/fesp.h"`

### 模块表

| 模块 | 职责 |
|------|------|
| `main.c` | 程序入口，窗口过程，消息分发 |
| `app_commands.c/h` | 菜单和工具栏命令处理 |
| `app_logview.c/h` | 日志视图和字体管理 |
| `serial.c/h` | 串口通信，数据收发，信号控制 |
| `device_file.c/h` | .esp 设备文件格式读写 |
| `esptool_hal.h/c` | 平台合同：回调 + 工具函数转发 |
| `fesptool/slip.c/h` | SLIP 编解码 |
| `fesptool/chip.c/h` | 芯片模拟（init、寄存器、MAC、启动日志） |
| `fesptool/chip_priv.h` | 芯片内部常量和函数声明 |
| `fesptool/efuse.c/h` | eFuse 模拟（控制器、读写、字段查询） |
| `fesptool/efuse_priv.h` | eFuse 内部常量和函数声明 |
| `fesptool/flash.c/h` | Flash 存储模拟 |
| `fesptool/esptool.c/h` | esptool 命令解析与响应 |
| `fesptool/esptool_priv.h` | 协议内部声明 |
| `fesptool/fesp.h` | 公开头文件聚合器 |

### esptool_hal 接口

模拟引擎的所有外部依赖汇聚到 `esptool_hal.h` 一个文件。移植时只需替换此头文件和对应的 `.c` 实现。

**输出回调（引擎 → 外部）：**

| 回调 | 说明 |
|---|---|
| `fesp_hal_write(data, len)` | 写数据到串口 |
| `fesp_hal_set_baud_rate(baud)` | 波特率切换 |
| `fesp_hal_modified()` | 设备修改通知 |
| `FESP_HAL_LOGI(tag, fmt, ...)` | Info 日志（GUI 窗口） |
| `FESP_HAL_LOGE(tag, fmt, ...)` | Error 日志（GUI 窗口） |

**工具函数（引擎 ← 平台实现）：**

| 函数 | 说明 |
|---|---|
| `fesp_hal_mem_alloc/zero_alloc/free` | 内存管理 |
| `fesp_hal_md5_calc` | MD5 哈希 |
| `fesp_hal_deflate_init/decompress` | DEFLATE 解压 |
| `fesp_hal_encrypt_init/data`, `decrypt_data` | AES-XTS 加解密 |

**Debug 日志宏：**

```c
FESP_HAL_LOGD(TAG, "debug message");  // 编译期可控（ENABLE_TRACE）
```

**GUI 注册函数（PascalCase）：**

| 函数 | 说明 |
|---|---|
| `FEsptoolSetWriteCallback(cb)` | 注册串口写回调 |
| `FEsptoolSetBaudRateCallback(cb)` | 注册波特率回调 |
| `FEsptoolSetModifiedCallback(cb)` | 注册设备修改回调 |
| `FEsptoolSetLogCallback(cb, ctx)` | 注册日志回调 |

## 编译

### CMake

```powershell
# 配置
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -B build

# 编译（Release）
cmake --build build --config Release -j

# 编译（Debug）
cmake --build build --config Debug

# 启用调试日志
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DENABLE_TRACE_PROTO=ON -DENABLE_TRACE_FW=ON -B build
cmake --build build --config Release -j

# 禁用 RX/TX hex dump 中的 ASCII 解码（减少日志 tokens，适合 Agents 分析）
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DLOG_NOT_SHOW_ASCII=ON -B build
cmake --build build --config Release -j

# 启用内存泄漏追踪
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DENABLE_MEM_DEBUG=ON -B build
cmake --build build --config Release -j
```

**构建选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `ENABLE_TRACE_FW` | OFF | 启用框架调试日志（TRACE_FW） |
| `ENABLE_TRACE_PROTO` | OFF | 启用协议调试日志（TRACE_PROTO） |
| `LOG_NOT_SHOW_ASCII` | OFF | 禁用 RX/TX hex dump 中的 ASCII 解码 |
| `ENABLE_MEM_DEBUG` | OFF | 启用内存分配泄漏追踪 |

输出：`build/Release/FakeEsptool.exe`（Release）或 `build/Debug/FakeEsptool.exe`（Debug）

### Pelles C

项目提供 Pelles C 项目文件 `FakeEsptool.ppj`，可使用 Pelles C 14.10 构建。

**环境要求：**
- Pelles C 14.10 或更高版本
- 安装路径：`C:\Program Files\PellesC`（默认）

**构建命令：**

```powershell
# 设置环境变量
$env:PATH = "C:\Program Files\PellesC\Bin;$env:PATH"
$env:PellesCDir = "C:\Program Files\PellesC"

# 构建（Release）
pomake /f FakeEsptool.ppj

# 构建（Debug）
pomake /f FakeEsptool.ppj "POC_PROJECT_MODE=Debug"
```

**项目文件：**
- `FakeEsptool.ppj` - Pelles C 项目文件
- `FakeEsptool.ppx` - Pelles C 工作区设置

**输出：** `FakeEsptool.exe`（项目根目录）

**注意事项：**
- Pelles C 使用 `-arch:AVX2` 编译选项，需要支持 AVX2 的 CPU
- Release 模式启用 `-Ot -Ox -Ob1` 优化
- Debug 模式启用 `-Zi` 调试信息并定义 `ENABLE_TRACE_FW=1` 和 `ENABLE_TRACE_PROTO=1`

### 测试

```powershell
cmake -B build_tests -S tests
cmake --build build_tests --config Release -j
ctest --test-dir build_tests --build-config Release
```

## esptool 协议

### SLIP 封装

```
0xC0 [payload] 0xC0

转义规则:
0xC0 → 0xDB 0xDC
0xDB → 0xDB 0xDD
```

### 命令格式

请求:
```
[dir=0x00][cmd][size_lo][size_hi][val_4bytes][data...][checksum]
```

响应:
```
[dir=0x01][cmd][size_lo][size_hi][status_4bytes][data...]
```

### 支持的命令

| 码 | 名称 | 说明 |
|----|------|------|
| 0x02 | FLASH_BEGIN | Flash 写入开始（擦除指定区域） |
| 0x03 | FLASH_DATA | Flash 写入数据 |
| 0x04 | FLASH_END | Flash 写入结束 |
| 0x05 | MEM_BEGIN | 内存写入开始 |
| 0x06 | MEM_END | 内存写入结束 |
| 0x07 | MEM_DATA | 内存写入数据 |
| 0x08 | SYNC | 同步握手 |
| 0x09 | WRITE_REG | 写寄存器 |
| 0x0A | READ_REG | 读寄存器 |
| 0x0B | SPI_SET_PARAMS | 设置 SPI Flash 参数 |
| 0x0D | SPI_ATTACH | 附加 SPI Flash |
| 0x0F | CHANGE_BAUDRATE | 修改波特率 |
| 0x10 | FLASH_DEFL_BEGIN | 压缩写入开始（擦除指定区域） |
| 0x11 | FLASH_DEFL_DATA | 压缩写入数据 |
| 0x12 | FLASH_DEFL_END | 压缩写入结束 |
| 0x13 | SPI_FLASH_MD5 | 计算Flash MD5 |
| 0x14 | GET_SECURITY_INFO | 获取安全信息 |
| 0xD0 | ERASE_FLASH | 擦除整个Flash |
| 0xD1 | ERASE_REGION | 擦除Flash区域 |
| 0xD2 | READ_FLASH | 读取Flash |
| 0xD3 | RUN_USER_CODE | 运行用户代码（软复位） |
| 0xD5 | SPI_NAND_ATTACH | 附加 SPI NAND Flash（未实现） |
| 0xD6 | SPI_NAND_READ_SPARE | 读取 NAND 备用区域（未实现） |
| 0xD7 | SPI_NAND_WRITE_SPARE | 写入 NAND 备用区域（未实现） |
| 0xD8 | SPI_NAND_READ_FLASH | 读取 NAND Flash（未实现） |
| 0xD9 | SPI_NAND_WRITE_FLASH_BEGIN | NAND Flash 写入开始（未实现） |
| 0xDA | SPI_NAND_WRITE_FLASH_DATA | NAND Flash 写入数据（未实现） |
| 0xDB | SPI_NAND_ERASE_FLASH | 擦除整个 NAND Flash（未实现） |
| 0xDC | SPI_NAND_ERASE_REGION | 擦除 NAND Flash 区域（未实现） |
| 0xDD | SPI_NAND_READ_PAGE_DEBUG | 读取 NAND 页面（调试，未实现） |
| 0xDE | SPI_NAND_WRITE_FLASH_END | NAND Flash 写入结束（未实现） |

## 芯片支持

| 枚举值 | 芯片 |
|--------|------|
| FESP_CHIP_ESP8266 | ESP8266 |
| FESP_CHIP_ESP32 | ESP32 |
| FESP_CHIP_ESP32S2 | ESP32-S2 |
| FESP_CHIP_ESP32S3 | ESP32-S3 |
| FESP_CHIP_ESP32C2 | ESP32-C2 |
| FESP_CHIP_ESP32C3 | ESP32-C3 |
| FESP_CHIP_ESP32C6 | ESP32-C6 |

**eFuse 初始化注意事项：**
- 新增芯片时必须在初始化函数中设置默认芯片版本到 eFuse，否则 esptool 可能禁用 stub flasher
- 各芯片 eFuse 版本字节位置（以代码 `chip.c` 实现为准）：
  - ESP32-C2：byte 0x46 |= 0x10（major=1, minor=0，bits[21:16] of EFUSE_BLOCK2_ADDR+4）
  - ESP32-S2：byte 0x51 |= 0x04（major=1，bits[19:18] of EFUSE_BLOCK1_ADDR+12）
  - ESP32-S3：byte 0x6C |= 0x01（blk_version_major=1）+ byte 0x52 |= 0x01（blk_version_minor=1）
  - ESP32-C3：byte 0x5B |= 0x01（major=1，bits[25:24] of EFUSE_BLOCK1_ADDR+20）
  - ESP32-C6：无芯片版本覆盖（major=0, minor=0）
- 有 eFuse 控制器的芯片（C2/C3/C6）必须在初始化中设置 `efuse_conf_ofs` 和 `efuse_cmd_ofs`：
  - ESP32-C2：`efuse_conf_ofs = 0x8C`，`efuse_cmd_ofs = 0x94`
  - ESP32-C3：`efuse_conf_ofs = 0x1CC`，`efuse_cmd_ofs = 0x1D4`
  - ESP32-C6：`efuse_conf_ofs = 0x1CC`，`efuse_cmd_ofs = 0x1D4`
- 各芯片 eFuse 寄存器基址（`efuse_base`，用于 `Chip_ReadReg` 地址映射）：
  - ESP8266：`0x3FF00000`（MAC 在偏移 0x50/0x54）
  - ESP32：`0x3FF5A000`（EFUSE_RD_REG_BASE）
  - ESP32-S2/S3/C3/C6：各自 EFUSE_BASE

### 加密烧录支持

**概述**：加密烧录是 ESP 芯片的安全功能，客户端发送明文数据，设备端使用 eFuse 中的密钥加密后写入 Flash。

**各芯片支持情况**：

| 芯片 | ROM 模式 | Stub 模式 | 说明 |
|------|---------|----------|------|
| ESP8266 | ❌ | ❌ | 芯片不支持 Flash 加密 |
| ESP32 | ❌ | ✅ | ROM 不支持扩展参数格式 |
| ESP32-S2/S3/C2/C3/C6 | ✅ | ✅ | |

**协议说明**：
- `FLASH_BEGIN (0x02)` 和 `FLASH_DEFL_BEGIN (0x10)` 支持 `encrypted` 字段
- `encrypted=1` 告诉设备"请在写入前加密数据"，客户端发送明文数据
- 加密烧录时，客户端会跳过 MD5 验证

**密钥管理**：
- 密钥存储在芯片的 eFuse 中（BLOCK_KEY0）
- 通过 Key Management 对话框管理密钥的导入、导出和生成
- 密钥文件格式：纯二进制（128-bit 或 256-bit）

**实现要点**：
- 使用 AES-XTS 算法进行加密/解密
- 参考 espsecure 的 `_flash_encryption_operation_esp32` 实现
- 加密写入和解密读取需要成对实现

## 使用示例

```c
#include "fesptool/fesp.h"
#include "fesptool_hal.h"

// 初始化
fesp_chip_init(&g_chip, FESP_CHIP_ESP32);
fesp_flash_init(&g_flash, 4 * 1024 * 1024);
fesp_init(&g_esptool, &g_chip, &g_flash);

// 注册 HAL 回调
FEsptoolSetWriteCallback(OnSerialWrite);
FEsptoolSetBaudRateCallback(OnBaudRateChange);
FEsptoolSetModifiedCallback(OnDeviceModified);
FEsptoolSetLogCallback(OnHalLog, NULL);
```

## API 参考

详见 [API.md](API.md)。

## 实现说明

### 单实例模式

**实现方式**：互斥量 + 窗口消息

**流程**：
1. 程序启动时创建命名互斥量 `FakeEsptool_SingleInstance_Mutex`
2. 如果互斥量已存在，说明已有实例运行
3. 通过 `FindWindowW(L"FakeEsptoolClass", NULL)` 查找已有窗口
4. 使用 `WM_COPYDATA` 消息传递文件路径给已有实例
5. 已有实例处理文件打开，新实例退出

**关键常量**：
```c
#define SINGLE_INSTANCE_MUTEX L"FakeEsptool_SingleInstance_Mutex"
```

**窗口查找**：
```c
HWND hExistingWnd = FindWindowW(L"FakeEsptoolClass", NULL);
```

**消息传递**：
```c
COPYDATASTRUCT cds = {0};
cds.dwData = 0;
cds.cbData = (DWORD)((wcslen(filePath) + 1) * sizeof(WCHAR));
cds.lpData = (void *)filePath;
SendMessageW(hExistingWnd, WM_COPYDATA, 0, (LPARAM)&cds);
```

### 命令行文件打开

**实现位置**：`wWinMain` + `Main_OnAppInit`

**流程**：
1. `wWinMain` 解析 `lpCmdLine` 获取文件路径
2. 使用 `GetFullPathNameW` 转换为绝对路径
3. 如果检测到已有实例，通过 `WM_COPYDATA` 发送文件路径
4. 如果是首次启动，将文件路径通过 `WM_APP_INIT` 的 lParam 传递给 `Main_OnAppInit`
5. `Main_OnAppInit` 优先加载命令行文件，其次检查上次文件

**路径解析**：
- 支持带引号的路径：`"C:\path\to\file.esp"`
- 支持不带引号的路径：`C:\path\to\file.esp`

### 拖放文件打开

**实现方式**：`WM_DROPFILES` 消息

**初始化**：
```c
// Main_OnCreate 中启用拖放
DragAcceptFiles(hWnd, TRUE);
```

**消息处理**：
```c
case WM_DROPFILES:
    return Main_OnDropFiles(hWnd, wParam, lParam);
```

**Main_OnDropFiles 流程**：
1. `DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0)` 获取文件数量
2. `DragQueryFileW(hDrop, 0, filePath, MAX_PATH)` 获取第一个文件路径
3. 检查文件扩展名是否为 `.esp`
4. 多文件时提示用户只打开第一个
5. 调用 `Main_OpenDeviceFile` 打开文件
6. `DragFinish(hDrop)` 释放资源

**Main_OpenDeviceFile 函数**：
- 复用 `PromptDisconnectIfNeeded` 和 `PromptSaveIfNeeded`
- 调用 `Device_Load` 加载设备文件
- 更新 UI 状态和配置

### Dump Device As 功能

**实现方式**：快照 + 后台线程

**流程**：
1. 主线程生成设备数据快照（eFuse + Flash）
2. 创建后台线程执行文件写入
3. 主线程显示忙碌光标并禁用窗口
4. 后台线程完成后通过 `WM_DUMP_COMPLETE` 消息通知主线程
5. 主线程恢复窗口状态

**数据结构**：
```c
typedef struct {
    fesp_chip_ctx_t chip;        /* Chip context snapshot */
    fesp_flash_ctx_t flash;      /* Flash context snapshot */
    WCHAR deviceFile[MAX_PATH];  /* Device file path */
    uint8_t *efuse;              /* eFuse data snapshot */
    DWORD efuseSize;             /* eFuse size */
    uint8_t *flashData;          /* Flash data snapshot */
    DWORD flashSize;             /* Flash size */
    WCHAR filename[MAX_PATH];    /* Output filename */
    HWND hWnd;                   /* Owner window */
} DEVICE_SNAPSHOT;
```

**自定义消息**：
- `WM_APP_INIT` (WM_USER + 100)：应用初始化
  - wParam：未使用
  - lParam：命令行文件路径（WCHAR*），可为 NULL
- `WM_DUMP_COMPLETE` (WM_USER + 101)：后台线程完成通知
  - wParam：成功标志 (BOOL)
  - lParam：错误代码（失败时）

### 忙碌处理模式

对于耗时操作（Flash 导入/导出、设备 Dump），采用以下模式：

```c
/* 1. 生成快照（如需要） */
BYTE *snapshot = HeapAlloc(GetProcessHeap(), 0, size);
memcpy(snapshot, data, size);

/* 2. 显示忙碌状态 */
HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
EnableWindow(hWnd, FALSE);

/* 3. 执行操作 */
DoWork(snapshot);

/* 4. 恢复状态 */
EnableWindow(hWnd, TRUE);
SetCursor(hOldCursor);
HeapFree(GetProcessHeap(), 0, snapshot);
```

## 编码规范

### 内存管理

协议层和设备层通过 `fesptool_hal` 使用内存管理函数，**禁止**直接调用 `HeapAlloc`/`HeapFree`。

**函数：**

```c
void *fesp_hal_mem_alloc(DWORD size);          // 未初始化分配
void *fesp_hal_mem_zero_alloc(DWORD size);      // 零初始化分配
void  fesp_hal_mem_free(void *ptr);             // 释放（NULL 安全）
```

**规范：**

```c
// ✗ 错误 - 直接调用 Heap API
void *ptr = HeapAlloc(GetProcessHeap(), 0, size);
void *ptr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
HeapFree(GetProcessHeap(), 0, ptr);

// ✓ 正确 - 通过 HAL
void *ptr = fesp_hal_mem_alloc(size);
void *ptr = fesp_hal_mem_zero_alloc(size);
fesp_hal_mem_free(ptr);
```

**示例：**

```c
uint8_t *efuse = (uint8_t *)fesp_hal_mem_zero_alloc(efuse_size);
if (!efuse) {
    FESP_HAL_LOGD(TAG, "Failed to allocate eFuse");
    return false;
}

// 使用 efuse...

fesp_hal_mem_free(efuse);
efuse = NULL;
```

**调试模式：**

启用 CMake 选项 `-DENABLE_MEM_DEBUG=ON` 启用内存泄漏追踪：
- 维护全局分配链表，记录每个分配的文件、行号、大小
- 程序退出时调用 `Mem_ReportLeaks()` 输出未释放的分配
- 提供 `Mem_GetAllocCount()` 和 `Mem_GetTotalAllocSize()` 查询当前分配状态
- Release 模式下零开销（宏展开为空）

**注意事项：**
- 始终检查 `Mem_Alloc`/`Mem_ZeroAlloc` 返回值，失败时返回 `NULL`
- 释放后将指针置为 `NULL`，防止悬挂指针
- GUI 层（`app_commands.c`、`app_logview.c`、`serial.c`、`dlg/*.c`）可继续使用 `HeapAlloc`/`HeapFree`

## 调试

### 启用日志

```powershell
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DENABLE_TRACE_PROTO=ON -DENABLE_TRACE_FW=ON -B build
cmake --build build --config Release -j
```

### 日志

模拟引擎使用三级日志，通过 `fesptool_hal.h` 统一接口：

```c
#include "fesptool_hal.h"

static const char *TAG = "ESP";

// Debug 级别 — trace 文件输出（编译期可控）
FESP_HAL_LOGD(TAG, "key_offset=0x%02X", offset);

// Info 级别 — GUI 窗口输出（始终启用）
EsptoolWrap_Log(TAG, "Sync handshake");

// Error 级别 — GUI 窗口输出（始终启用，可标红）
EsptoolWrap_Log("ERR", "Encryption failed: %d", ret);
```

| 级别 | 宏/函数 | 去向 | 编译控制 |
|---|---|---|---|
| Debug | `FESP_HAL_LOGD(TAG, ...)` | trace .log 文件 | `ENABLE_TRACE` |
| Info | `EsptoolWrap_Log(tag, fmt, ...)` | GUI 窗口 | 始终启用 |
| Error | `EsptoolWrap_Log(tag, fmt, ...)` | GUI 窗口（可标红） | 始终启用 |

### Trace 日志格式

Trace 日志输出到与可执行文件同目录的 `.log` 文件，格式：

```
HH:MM:SS.mmm +S.mmm [thread_id] [TAG] message
```

- `HH:MM:SS.mmm` - 绝对时间（时:分:秒.毫秒）
- `+S.mmm` - 相对时间（距上一条 trace 的秒数.毫秒）
- `thread_id` - 线程 ID
- `TAG` - 日志标签（如 `ESP`、`SER`、`GUI`）
- `message` - 日志内容

**注意：** `%ls`（宽字符串格式符）是 MSVC 扩展，非标准 C。在本项目 MSVC 编译环境下可正常使用。

示例：
```
08:58:38.094 +0.000 [18628] [ESP] RX frame len=44
08:58:38.095 +0.001 [18628] [ESP] SYNC received
08:58:38.121 +0.026 [18628] [ESP] SendResponse cmd=0x08
```

## 测试

使用 esptool 测试:

```bash
# 读取MAC
esptool --port COM10 read-mac

# 读取Flash
esptool --port COM10 read-flash 0 0x1000 flash.bin

# 写入Flash
esptool --port COM10 write-flash 0 firmware.bin

# 擦除Flash
esptool --port COM10 erase-flash
```

### 注意事项

**GET_SECURITY_INFO 响应格式：**
- 响应 Data 字段中 status 字节必须放在**末尾**，不能放在开头
- `chip_id` 必须使用 IMAGE_CHIP_ID（如 `13`），不能使用 magic value（如 `0x2CE0806F`）
- 不同芯片支持情况不同：
  - ESP8266/ESP32：不支持，应返回 `ROM_INVALID_RECV_MSG` 错误
  - ESP32-S2：返回 14 字节响应（无 chip_id）
  - ESP32-S3/C2/C3/C6：返回 22 字节响应 `[payload:20][status:2]`，包含 IMAGE_CHIP_ID

### 烧录验证工具

**位置**：`tools/verify_flash.py`

**功能**：验证 esp-idf 构建产物是否正确烧录到 FakeEsptool 设备文件中。

**用途**：用于测试烧录功能的正确性。在 FakeEsptool 中完成烧录并保存设备文件后，使用此脚本对比原始烧录文件与设备文件中的 Flash 数据。

**使用方法**：

```bash
# WSL 环境
python3 tools/verify_flash.py <烧录目录> <设备文件>

# 示例
python3 tools/verify_flash.py build/my_project my_device.esp
```

**输入参数**：
- `烧录目录`：esp-idf 构建产物目录，包含 `flash_args` 文件和 .bin 文件
- `设备文件`：FakeEsptool 保存的 .esp 设备文件

**工作原理**：
1. 解析烧录目录中的 `flash_args` 文件，获取烧录地址和文件路径
2. 读取设备文件头，获取 Flash 大小和 eFuse 大小
3. 跳过 eFuse 数据，提取 Flash 数据
4. 对每个烧录文件，对比其内容与设备文件中对应地址的数据

**输出示例**：
```
Flash directory: build/my_project
  - Offset: 0x00000000
    File: bootloader/bootloader.bin
    File Size: 19696 bytes
    File MD5: f429d996716eafd005d95da5c9cb9152
  - Offset: 0x00010000
    File: my_app.bin
    File Size: 107744 bytes
    File MD5: 9d3e1314ea0274e80f2d177f821bc65f
  - Offset: 0x00008000
    File: partition_table/partition-table.bin
    File Size: 3072 bytes
    File MD5: 5d61d196adc3dba01928f264eb169be7

Device file: my_device.esp
  File MD5: 1d96ceb1e4a9934fabacd427f10efe04
  Chip Type: ESP32-C2 (4)
  XTAL Freq: 26MHz (1)
  MAC: AA:BB:CC:DD:EE:01
  eFuse Size: 128 bytes
  Flash Size: 2097152 bytes (2.0 MB)
  Flash MD5: 761e417679ec198ecae170a53c98863e

Verify:
  [PASS] 0x00000000 bootloader/bootloader.bin (19696 bytes)
  [PASS] 0x00010000 my_app.bin (107744 bytes)
  [PASS] 0x00008000 partition_table/partition-table.bin (3072 bytes)

All flash segments verified successfully.
```

**返回值**：
- `0`：所有烧录段验证通过
- `1`：存在验证失败的烧录段

### 单元测试框架

**位置**：`tests/`

**测试文件**：

| 文件 | 测试内容 |
|------|----------|
| `test_deflate.c` | DEFLATE 解压器测试 |
| `test_encrypt.c` | AES-XTS 加密/解密测试 |

**测试数据**：

| 文件 | 说明 |
|------|------|
| `deflate_test_data.h` | DEFLATE 测试数据（由 `generate_deflate_test_data.py` 生成） |
| `encrypt_test_data.h` | 加密测试数据（由 `generate_encrypt_test_data.py` 生成） |
| `test_data/` | 加密测试数据目录（.bin 文件，可重新生成） |

**添加新测试**：

1. 在 `tests/` 目录创建新的测试文件（如 `test_xxx.c`）
2. 更新 `tests/CMakeLists.txt`，添加新的测试目标
3. 使用 `TEST_ASSERT` 宏编写测试用例
4. 运行测试验证

### CI/CD

**GitHub Actions 工作流**：`.github/workflows/build.yml`

**构建流程**：
1. 配置测试 → 编译测试 → 运行测试
2. 配置应用 → 编译应用
3. 打包发布（仅 tag 触发）

**测试通过条件**：所有单元测试必须通过，否则应用构建不会执行。

---

## 积累解压方案实现细节

### 数据结构

`fesp_ctx_t` 中新增以下字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `defl_buf` | uint8_t* | 压缩数据积累缓冲区 |
| `defl_buf_size` | uint32_t | 当前积累的压缩数据大小 |
| `defl_buf_cap` | uint32_t | 缓冲区容量（等于 `uncompressed_size`） |
| `defl_offset` | uint32_t | 当前 deflate 会话的 Flash 写入偏移 |
| `defl_unc_size` | uint32_t | 当前 deflate 会话的未压缩大小 |

### 辅助函数

| `defl_free_buffer` | 释放积累缓冲区，不写入 flash。用于错误处理和资源清理。 |

| `defl_flush_buffer` | 解压积累数据并写入 flash，然后释放缓冲区。返回 `FESP_OK` 或 `FESP_FAIL`。 |

### 函数修改说明

| 函数 | 修改内容 |
|------|----------|
| `fesp_init` | 初始化 `defl_buf = NULL`，`defl_buf_size = 0`，`defl_buf_cap = 0` |
| `fesp_reset_state` | 调用 `Defl_FreeBuffer()`，重置所有 deflate 字段 |
| `handle_flash_defl_begin` | 检查并处理上一次积累数据，分配新缓冲区 |
| `handle_flash_defl_data` | 积累数据到缓冲区，不立即解压 |
| `handle_flash_defl_end` | 调用 `defl_flush_buffer` 解压并写入 |
| `handle_flash_begin` | **不释放**缓冲区（等待后续 `FLASH_DEFL_END`） |
| `handle_flash_end` | 检查并 flush 未处理的缓冲区（ROM 模式场景） |
| `handle_erase_flash` | 调用 `defl_free_buffer` 释放缓冲区 |
| `handle_erase_block` | 调用 `defl_free_buffer` 释放缓冲区 |

### 资源释放检查点

| 检查点 | 操作 | 原因 |
|--------|------|------|
| `FLASH_DEFL_END` | Flush + Free | 正常结束压缩写入（Stub 模式） |
| `FLASH_DEFL_BEGIN`（重复） | Flush + Free | ROM 模式多文件烧录不发 END |
| `FLASH_BEGIN` | **不释放** | 客户端可能在 `FLASH_DEFL_DATA` 后发送 `FLASH_BEGIN`，再发送 `FLASH_DEFL_END` |
| `FLASH_END` | Flush（如有）+ Free | ROM 模式可能直接发 `FLASH_END` 结束压缩烧录 |
| `ERASE_FLASH` | Free | 擦除操作中断烧录 |
| `ERASE_REGION` | Free | 擦除操作中断烧录 |
| `RUN_USER_CODE` | Free | 软复位 |
| `fesp_reset_state` | Free | 硬件复位 |

### 时序分析

**正常流程（Stub 模式）：**
```
FLASH_DEFL_BEGIN → 分配缓冲区
FLASH_DEFL_DATA × N → 积累数据
FLASH_DEFL_END → 解压 → 写入 flash → 释放缓冲区
```

**多文件烧录（ROM 模式）：**
```
FLASH_DEFL_BEGIN (文件1) → 分配缓冲区
FLASH_DEFL_DATA × N → 积累数据
FLASH_DEFL_BEGIN (文件2) → 解压文件1 → 写入 flash → 释放 → 分配新缓冲区
FLASH_DEFL_DATA × N → 积累数据
```

**客户端异常流程：**
```
FLASH_DEFL_BEGIN → 分配缓冲区
FLASH_DEFL_DATA → 积累数据
FLASH_BEGIN → 保留缓冲区
FLASH_DEFL_END → 解压 → 写入 flash → 释放缓冲区
```

### 超时风险

**场景：** ROM 模式多文件烧录时，`FLASH_DEFL_BEGIN` 需要先处理上一个文件的积累数据。

**风险：** 如果上一个文件很大（如几 MB 固件），解压 + 写入 flash 可能耗时较长，导致客户端对 `FLASH_DEFL_BEGIN` 响应超时。

**影响：** 仅影响 ROM 模式下的多文件烧录场景。

**缓解措施：**
- 解压和写入是同步操作，通常几 MB 数据在 1-2 秒内完成
- 客户端对 `FLASH_DEFL_BEGIN` 的超时通常为 3-10 秒
- 如果超时成为问题，可考虑在 `FLASH_DEFL_END` 时就处理（但 ROM 模式不发 END）

**开发建议：** 实现后使用 Python esptool 的 ROM 模式进行多文件烧录测试，验证是否超时。
