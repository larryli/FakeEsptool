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
| 0x02 | FLASH_BEGIN | Flash 写入开始 |
| 0x03 | FLASH_DATA | Flash 写入数据 |
| 0x04 | FLASH_END | Flash 写入结束 |
| 0x05 | MEM_BEGIN | 内存写入开始 |
| 0x06 | MEM_END | 内存写入结束 |
| 0x07 | MEM_DATA | 内存写入数据 |
| 0x08 | SYNC | 同步握手 |
| 0x09 | WRITE_REG | 写寄存器 |
| 0x0A | READ_REG | 读寄存器 |
| 0x0F | CHANGE_BAUDRATE | 修改波特率 |
| 0x10 | FLASH_DEFL_BEGIN | 压缩写入开始 |
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

| 函数 | 说明 |
|------|------|
| `Flash_Init(ctx, size)` | 初始化Flash |
| `Flash_Close(ctx)` | 释放Flash |
| `Flash_Read(ctx, addr, buf, len)` | 读取数据 |
| `Flash_Write(ctx, addr, data, len)` | 写入数据 |
| `Flash_Erase(ctx, addr, len)` | 擦除区域 |
| `Flash_EraseAll(ctx)` | 擦除全部 |
| `Flash_CalcMd5(ctx, addr, len, md5)` | 计算MD5 |

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

| 函数 | 说明 |
|------|------|
| `Device_Init(ctx, chipType, flashSize, mac)` | 初始化设备 |
| `Device_Close(ctx)` | 关闭设备 |
| `Device_Save(ctx, filename)` | 保存设备文件 |
| `Device_Load(ctx, filename)` | 加载设备文件 |
| `Device_IsModified(ctx)` | 检查是否已修改 |
| `Device_SetModified(ctx, modified)` | 设置修改标记 |
| `Device_GetFilename(ctx)` | 获取文件名 |

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
