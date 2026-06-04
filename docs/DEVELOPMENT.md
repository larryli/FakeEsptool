# FakeEsptool 开发文档

## 项目概述

FakeEsptool 是一个 ESP 芯片设备端模拟器，用于模拟 ESP8266/ESP32 系列芯片响应 esptool 客户端的烧录协议。

## 架构概览

```
┌─────────────┐     ┌─────────────┐     ┌───────────────────┐
│   main.c    │────▶│  serial.c   │────▶│  esptool/esptool.c│
│  (UI层)     │     │ (通信层)    │     │   (协议层)        │
└─────────────┘     └─────────────┘     └────────┬──────────┘
                                                  │
                               ┌──────────────────┼──────────────────┐
                               ▼                  ▼                  ▼
                     ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
                     │   slip.c    │    │  esptool.c  │    │  device.c   │
                     │ (SLIP编解码) │    │ (命令处理)   │    │(设备文件管理)│
                     └─────────────┘    └─────────────┘    └─────────────┘
                             │                  │
                     ┌───────┴───────┐  ┌───────┴───────┐
                     ▼               ▼  ▼               ▼
               ┌──────────┐   ┌──────────┐
               │  chip.c  │   │  flash.c │
               │ (芯片特性)│   │(Flash存储)│
               └──────────┘   └──────────┘
```

| 模块 | 职责 |
|------|------|
| `main.c` | 用户界面，菜单、工具栏、日志显示，esptool 回调注册与设备同步 |
| `serial.c` | 串口通信，数据收发，信号控制 |
| `esptool/slip.c/h` | SLIP 协议编解码 |
| `esptool/chip.c/h` | 芯片特性模拟（efuse、MAC等） |
| `esptool/flash.c/h` | Flash 存储模拟 |
| `esptool/device.c/h` | 设备文件管理 |
| `esptool/esptool.c/h` | esptool 命令解析与响应 |

## 编译

### CMake

```powershell
# 配置
cmake -S . -B build -G "NMake Makefiles"

# 编译
cmake --build build

# 启用调试日志
cmake -S . -B build -G "NMake Makefiles" -DENABLE_TRACE_PROTO=ON
cmake --build build
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
| 0x0F | CHANGE_BAUDRATE | 修改波特率 |
| 0x10 | FLASH_DEFL_BEGIN | 压缩写入开始（擦除指定区域） |
| 0x11 | FLASH_DEFL_DATA | 压缩写入数据 |
| 0x12 | FLASH_DEFL_END | 压缩写入结束 |
| 0x13 | SPI_FLASH_MD5 | 计算Flash MD5 |
| 0x14 | GET_SECURITY_INFO | 获取安全信息 |
| 0xD0 | ERASE_FLASH | 擦除整个Flash |
| 0xD1 | ERASE_REGION | 擦除Flash区域 |
| 0xD2 | READ_FLASH | 读取Flash |

## 芯片支持

| 枚举值 | 芯片 |
|--------|------|
| CHIP_ESP8266 | ESP8266 |
| CHIP_ESP32 | ESP32 |
| CHIP_ESP32S2 | ESP32-S2 |
| CHIP_ESP32S3 | ESP32-S3 |
| CHIP_ESP32C2 | ESP32-C2 |
| CHIP_ESP32C3 | ESP32-C3 |
| CHIP_ESP32C6 | ESP32-C6 |

## 使用示例

```c
#include "esptool/esptool.h"

// 初始化
Esptool_Init(&g_esptool);

// 设置芯片
Esptool_SetChipType(&g_esptool, CHIP_ESP32);
Esptool_SetFlashSize(&g_esptool, 4 * 1024 * 1024);

// 注册回调
Serial_SetReceiveCallback(&g_serial, (SERIAL_RX_CB)OnEsptoolProcessData);
Serial_SetSignalCallback(&g_serial, (SERIAL_SIGNAL_CB)OnEsptoolSignal);
```

## API 参考

### esptool.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `ESP_PACKET` | 协议数据包（约 32KB） |
| `ESPTOOL_CTX` | 协议上下文（包含 ESP_PACKET 预分配缓冲区） |

**ESP_PACKET 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `direction` | BYTE | 请求 (0x00) 或响应 (0x01) |
| `command` | BYTE | 命令码 |
| `size` | WORD | 数据载荷大小 |
| `value` | DWORD | 命令相关值 |
| `data[32760]` | BYTE | 数据载荷 |

**ESPTOOL_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `slip` | SLIP_CTX | SLIP 解码器上下文 |
| `chip` | CHIP_CTX | 芯片特性 |
| `flash` | FLASH_CTX | Flash 存储 |
| `pkt` | ESP_PACKET | 预分配数据包缓冲区（避免栈溢出） |
| `synced` | BOOL | SYNC 握手完成标志 |
| `stub_mode` | BOOL | Stub 运行标志 |
| `hNotify` | HWND | UI 通知窗口 |
| `onModified` | ESP_MODIFIED_CB | 设备修改回调 |
| `onWrite` | ESP_WRITE_CB | 串口写回调 |
| `onBaudRate` | ESP_BAUDRATE_CB | 波特率修改回调 |
| `flash_offset` | DWORD | 当前 Flash 写入偏移 |
| `flash_seq` | DWORD | 当前 Flash 写入序列号 |
| `last_read_val` | DWORD | 上次 READ_REG 的值 |

**函数：**

| 函数 | 说明 |
|------|------|
| `Esptool_Init(ctx)` | 初始化上下文 |
| `Esptool_SetNotify(ctx, hNotify)` | 设置通知窗口 |
| `Esptool_SetModifiedCallback(ctx, cb)` | 设置修改回调 |
| `Esptool_SetWriteCallback(ctx, cb)` | 设置串口写回调 |
| `Esptool_SetBaudRateCallback(ctx, cb)` | 设置波特率修改回调 |
| `Esptool_Feed(ctx, data, len)` | 喂入串口数据 |
| `Esptool_ProcessFrame(ctx, frame, frame_len)` | 处理一帧数据 |
| `Esptool_SetChipType(ctx, type)` | 设置芯片类型 |
| `Esptool_SetFlashSize(ctx, size)` | 设置Flash大小 |
| `Esptool_SendResponse(ctx, cmd, req_val, status, data, len)` | 发送响应 |
| `Esptool_CalcChecksum(data, len)` | 计算校验和 |

### chip.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `SPI_OFFSETS` | SPI 寄存器偏移（按芯片族区分） |
| `CHIP_CTX` | 芯片上下文（包含 SPI_OFFSETS 指针） |

**SPI_OFFSETS 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `usr` | BYTE | SPI_USR 偏移 |
| `usr1` | BYTE | SPI_USR1 偏移 |
| `usr2` | BYTE | SPI_USR2 偏移 |
| `w0` | BYTE | SPI_W0 偏移 |
| `mosi_dlen` | BYTE | SPI_MOSI_DLEN 偏移（0=不支持） |
| `miso_dlen` | BYTE | SPI_MISO_DLEN 偏移（0=不支持） |

**函数：**

| 函数 | 说明 |
|------|------|
| `Chip_Init(ctx, type)` | 初始化芯片 |
| `Chip_Close(ctx)` | 释放芯片 |
| `Chip_GetName(ctx)` | 获取芯片名称 |
| `Chip_SetMac(ctx, mac)` | 设置MAC地址 |
| `Chip_GetMac(ctx)` | 获取MAC地址 |
| `Chip_ReadReg(ctx, addr)` | 读取寄存器 |
| `Chip_WriteReg(ctx, addr, val)` | 写入寄存器 |
| `Chip_SetFlashSize(ctx, size)` | 设置Flash大小 |
| `Chip_GetFlashSize(ctx)` | 获取Flash大小 |
| `Chip_GetChipId(ctx)` | 获取芯片ID |
| `Chip_GetEfuse(ctx)` | 获取efuse数据 |
| `Chip_GetEfuseSize(ctx)` | 获取efuse大小 |
| `Chip_GetBootBaudRate(ctx)` | 获取启动日志波特率 |
| `Chip_GetBootMessage(ctx, reset_cause)` | 获取启动日志文本 |

### flash.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `FLASH_CTX` | Flash 存储上下文 |

**FLASH_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `data` | BYTE* | Flash 数据缓冲区 |
| `size` | DWORD | Flash 大小（字节） |
| `allocated` | BOOL | 缓冲区已分配标志 |

**函数：**

| 函数 | 说明 |
|------|------|
| `Flash_Init(ctx, size)` | 初始化 Flash |
| `Flash_Close(ctx)` | 释放 Flash |
| `Flash_Read(ctx, addr, buf, len)` | 读取数据 |
| `Flash_Write(ctx, addr, data, len)` | 写入数据（AND 操作，模拟真实 Flash 行为） |
| `Flash_Erase(ctx, addr, len)` | 擦除区域（自动 4KB 扇区对齐，设为 0xFF） |
| `Flash_EraseAll(ctx)` | 擦除全部 |
| `Flash_CalcMd5(ctx, addr, len, md5)` | 计算 MD5 |

**Flash_Write 行为说明：**
- 真实 Flash 存储器只能将位从 1 改为 0，不能从 0 改为 1
- 要将 0 改为 1，必须先擦除扇区（设为 0xFF）
- 此函数执行：`flash[i] &= data[i]`

### slip.h

| 函数 | 说明 |
|------|------|
| `Slip_Init(ctx)` | 初始化解码器 |
| `Slip_PutByte(ctx, b)` | 喂入字节 |
| `Slip_IsComplete(ctx)` | 检查帧完成 |
| `Slip_GetPayload(ctx)` | 获取载荷 |
| `Slip_GetLength(ctx)` | 获取长度 |
| `Slip_Reset(ctx)` | 重置状态 |
| `Slip_Encode(data, len, out, max)` | 编码一帧 |

### device.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `DEVICE_CTX` | 设备上下文（包含芯片、Flash、文件名、修改标记） |

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `DEVICE_MAGIC` | `0x45535000` | 文件魔数 ("ESP\0") |
| `DEVICE_VERSION` | `1` | 文件格式版本 |

**函数：**

| 函数 | 说明 |
|------|------|
| `Device_Init(ctx, chipType, flashSize, mac)` | 初始化设备 |
| `Device_Close(ctx)` | 释放设备资源 |
| `Device_Save(ctx, filename)` | 保存设备到 .esp 文件 |
| `Device_Load(ctx, filename)` | 从 .esp 文件加载设备 |
| `Device_IsModified(ctx)` | 检查是否已修改 |
| `Device_SetModified(ctx, modified)` | 设置修改标记 |
| `Device_GetFilename(ctx)` | 获取当前文件路径 |

### serial.h

| 函数 | 说明 |
|------|------|
| `Serial_Open(ctx, port, hNotify)` | 打开串口 |
| `Serial_Close(ctx)` | 关闭串口 |
| `Serial_IsOpen(ctx)` | 检查状态 |
| `Serial_WriteData(ctx, data, len, hNotify)` | 写入数据 |
| `Serial_SetReceiveCallback(ctx, cb)` | 设置接收回调 |
| `Serial_SetSignalCallback(ctx, cb)` | 设置信号回调 |
| `Serial_SetDtr(ctx, state)` | 设置DTR |
| `Serial_SetRts(ctx, state)` | 设置RTS |
| `Serial_SetBaudRate(ctx, baudRate)` | 修改波特率 |
| `Serial_SetDataBits(ctx, bits)` | 修改数据位 |
| `Serial_SetParity(ctx, parity)` | 修改校验 |
| `Serial_SetStopBits(ctx, bits)` | 修改停止位 |
| `Serial_GetConfig(ctx, ...)` | 读取配置 |
| `Serial_GetRxBytes(ctx)` | 获取接收字节数 |
| `Serial_GetTxBytes(ctx)` | 获取发送字节数 |
| `Serial_GetPortName(index, portName, maxLen)` | 获取端口名 |
| `Serial_PostLog(hNotify, tag, text)` | 发送日志 |
| `Serial_PostLogF(hNotify, tag, fmt, ...)` | 格式化日志 |

## 编码规范

### 内存管理

本项目使用 Windows Heap API 进行动态内存管理，**禁止**使用 C 标准库的 `malloc`/`calloc`/`realloc`/`free`。

**原因：**
- 统一使用 Windows 堆管理，便于调试和内存泄漏检测
- `HeapAlloc`/`HeapFree` 支持 `HEAP_ZERO_MEMORY` 标志替代 `calloc`
- 与项目中已有的 `HeapAlloc`/`HeapFree` 用法保持一致

**规范：**

```c
// ✗ 错误 - 使用 C 标准库
void *ptr = malloc(size);
void *ptr = calloc(count, size);
free(ptr);

// ✓ 正确 - 使用 Windows Heap API
void *ptr = HeapAlloc(GetProcessHeap(), 0, size);           // 未初始化
void *ptr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);  // 零初始化（替代 calloc）
HeapFree(GetProcessHeap(), 0, ptr);
```

**示例：**

```c
BYTE *efuse = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, efuse_size);
if (!efuse) {
    TRACE_FW(TAG, "Failed to allocate eFuse");
    return FALSE;
}

// 使用 efuse...

HeapFree(GetProcessHeap(), 0, efuse);
```

**注意事项：**
- 始终检查 `HeapAlloc` 返回值，失败时返回 `NULL`
- 使用 `HEAP_ZERO_MEMORY` 替代 `calloc` 的零初始化行为
- 使用 `GetProcessHeap()` 获取进程默认堆句柄
- 释放后将指针置为 `NULL`，防止悬挂指针

## 调试

### 启用日志

```powershell
cmake -S . -B build -G "NMake Makefiles" -DENABLE_TRACE_PROTO=ON -DENABLE_TRACE_FW=ON
cmake --build build
```

### 日志宏

```c
#include "utils/trace.h"

static const char *TAG = "ESP";

TRACE_FW(TAG, "Framework message");
TRACE_PROTO(TAG, "Protocol message: 0x%02X", cmd);
```

### 窗口日志

```c
Serial_PostLog(hNotify, L"ESP", L"Command received");
Serial_PostLogF(hNotify, L"ESP", L"Flash addr=0x%08lX", addr);
```

## 测试

使用 esptool.py 测试:

```bash
# 读取MAC
esptool.py --port COM10 read_mac

# 读取Flash
esptool.py --port COM10 read_flash 0 0x1000 flash.bin

# 写入Flash
esptool.py --port COM10 write_flash 0 firmware.bin

# 擦除Flash
esptool.py --port COM10 erase_flash
```
