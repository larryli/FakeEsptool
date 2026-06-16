# FakeEsptool 开发文档

## 项目概述

FakeEsptool 是一个 ESP 芯片设备端模拟器，用于模拟 ESP8266/ESP32 系列芯片响应 esptool 客户端的烧录协议。

## 架构概览

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   main.c    │────▶│  serial.c   │────▶│  esptool.c  │
│  (UI层)     │     │ (通信层)    │     │ (协议层)    │
└──────┬──────┘     └─────────────┘     └──────┬──────┘
       │                                       │
       │                              ┌────────┼────────┐
       │                              ▼        ▼        ▼
       │                        ┌─────────┐┌─────────┐┌─────────┐
       │                        │ slip.c  ││ chip.c  ││ flash.c │
       │                        │(SLIP编解码)││(芯片特性)││(Flash存储)│
       │                        └─────────┘└─────────┘└─────────┘
       │
       ▼
┌─────────────┐
│  device.c   │
│(设备文件管理)│
└─────────────┘
```

### 数据共享机制

协议层（`ESPTOOL_CTX`）通过指针直接引用设备层（`DEVICE_CTX`）的数据，无需数据复制或同步：

```
g_device.chip  ◄─── g_esptool.chip   (同一块内存)
g_device.flash ◄─── g_esptool.flash  (同一块内存)
```

**初始化**：
```c
Esptool_Init(&g_esptool, &g_device.chip, &g_device.flash);
```

**数据流**：
- esptool 协议层读写 `g_esptool.chip` / `g_esptool.flash` 时，直接修改 `g_device` 的数据
- 设备保存、导出等操作直接读取 `g_device` 的数据
- 无需额外的同步函数

| 模块 | 职责 |
|------|------|
| `main.c` | 程序入口，窗口过程，消息分发 |
| `app_commands.c/h` | 菜单和工具栏命令处理 |
| `app_logview.c/h` | 日志视图和字体管理 |
| `serial.c/h` | 串口通信，数据收发，信号控制 |
| `esptool/slip.c/h` | SLIP 协议编解码 |
| `esptool/chip.c/h` | 芯片特性模拟（efuse、MAC等） |
| `esptool/flash.c/h` | Flash 存储模拟 |
| `esptool/device.c/h` | 设备文件管理 |
| `esptool/esptool.c/h` | esptool 命令解析与响应 |
| `dlg/device_props.c` | 设备属性对话框 |
| `dlg/key_mgmt.c` | 密钥管理对话框 |
| `dlg/port_select.c` | 串口选择对话框 |
| `dlg/about.c` | 关于对话框 |
| `utils/config.c/h` | 配置持久化 |
| `utils/lang.c/h` | 国际化辅助 |
| `utils/trace.c/h` | 调试日志 |
| `utils/deflate.c/h` | DEFLATE 解压（用于压缩模式烧录） |
| `utils/encrypt.c/h` | AES-XTS 加密/解密（用于加密烧录） |

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
```

**构建选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `ENABLE_TRACE_FW` | OFF | 启用框架调试日志（TRACE_FW） |
| `ENABLE_TRACE_PROTO` | OFF | 启用协议调试日志（TRACE_PROTO） |
| `LOG_NOT_SHOW_ASCII` | OFF | 禁用 RX/TX hex dump 中的 ASCII 解码 |

输出：`build/Release/FakeEsptool.exe`（Release）或 `build/Debug/FakeEsptool.exe`（Debug）

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
| CHIP_ESP8266 | ESP8266 |
| CHIP_ESP32 | ESP32 |
| CHIP_ESP32S2 | ESP32-S2 |
| CHIP_ESP32S3 | ESP32-S3 |
| CHIP_ESP32C2 | ESP32-C2 |
| CHIP_ESP32C3 | ESP32-C3 |
| CHIP_ESP32C6 | ESP32-C6 |

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
#include "esptool/esptool.h"

// 初始化（协议层直接引用设备数据）
Esptool_Init(&g_esptool, &g_device.chip, &g_device.flash);

// 注册回调
Esptool_SetWriteCallback(&g_esptool, OnSerialWrite);
Esptool_SetBaudRateCallback(&g_esptool, OnBaudRateChange);
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
| `data[ESP_PACKET_DATA_MAX]` | BYTE | 数据载荷 |

**ESPTOOL_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `slip` | SLIP_CTX | SLIP 解码器上下文 |
| `chip` | CHIP_CTX* | 指向设备芯片数据（不拥有） |
| `flash` | FLASH_CTX* | 指向设备 Flash 数据（不拥有） |
| `pkt` | ESP_PACKET | 预分配数据包缓冲区（避免栈溢出） |
| `state` | ESP_STATE | 协议状态机 |
| `synced` | BOOL | SYNC 握手完成标志 |
| `stub_mode` | BOOL | Stub 运行标志 |
| `hNotify` | HWND | UI 通知窗口 |
| `onModified` | ESP_MODIFIED_CB | 设备修改回调 |
| `onWrite` | ESP_WRITE_CB | 串口写回调 |
| `onBaudRate` | ESP_BAUDRATE_CB | 波特率修改回调 |
| `flash_offset` | DWORD | 当前 Flash 写入偏移 |
| `flash_seq` | DWORD | 当前 Flash 写入序列号 |
| `last_read_val` | DWORD | 上次 READ_REG 的值 |
| `flash_uncompressed_size` | DWORD | DEFLATE 解压大小 |
| `defl_buf` | BYTE* | 压缩数据积累缓冲区 |
| `defl_buf_size` | DWORD | 当前积累数据大小 |
| `defl_buf_cap` | DWORD | 缓冲区容量 |
| `defl_offset` | DWORD | 当前 deflate 会话的 Flash 偏移 |
| `defl_unc_size` | DWORD | 当前 deflate 会话的未压缩大小 |

**ESP_STATE 枚举：**

| 值 | 说明 |
|------|------|
| `ESP_STATE_IDLE` | 初始状态，等待 SYNC |
| `ESP_STATE_SYNCED` | 已同步，等待芯片检测 |
| `ESP_STATE_READY` | 芯片已检测，可接受命令 |
| `ESP_STATE_FLASH_WRITING` | FLASH_BEGIN 已发送，等待数据 |
| `ESP_STATE_MEM_WRITING` | MEM_BEGIN 已发送，等待数据 |

**状态转换规则：**

| 命令 | 转换 |
|------|------|
| SYNC | → SYNCED |
| READ_REG (0x40001000) | SYNCED → READY |
| FLASH_BEGIN / FLASH_DEFL_BEGIN | → FLASH_WRITING |
| FLASH_END / FLASH_DEFL_END | → READY |
| MEM_BEGIN | → MEM_WRITING |
| MEM_END | → READY |
| RUN_USER_CODE | → IDLE |

**函数：**

| 函数 | 说明 |
|------|------|
| `Esptool_Init(ctx, chip, flash)` | 初始化上下文，绑定设备数据指针 |
| `Esptool_ResetState(ctx)` | 重置协议状态（进入下载模式时调用） |
| `Esptool_SetNotify(ctx, hNotify)` | 设置通知窗口 |
| `Esptool_SetModifiedCallback(ctx, cb)` | 设置修改回调 |
| `Esptool_SetWriteCallback(ctx, cb)` | 设置串口写回调 |
| `Esptool_SetBaudRateCallback(ctx, cb)` | 设置波特率修改回调 |
| `Esptool_Feed(ctx, data, len)` | 喂入串口数据 |
| `Esptool_ProcessFrame(ctx, frame, frame_len)` | 处理一帧数据 |
| `Esptool_SendResponse(ctx, cmd, req_val, status, data, len)` | 发送响应（4字节状态） |
| `Esptool_SendResponseEx(ctx, cmd, req_val, status, status_len, data, len)` | 发送响应（可配置状态长度） |
| `Esptool_CalcChecksum(data, len)` | 计算校验和 |

**注意：** `SendResponseEx` 的 `status` 参数仅用于日志记录（TRACE_PROTO 和 Serial_PostLogF），不包含在响应包中。`status_len` 参数决定响应 Data 字段的总长度（`data_len` + 状态码），当 `data=NULL` 且 `data_len>0` 时，函数自动填充零字节（表示成功）。`SendResponse` 是 `SendResponseEx` 的便捷封装，固定使用 4 字节状态长度。

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

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `CHIP_DETECT_REG` | `0x40001000` | 芯片检测魔数寄存器地址（用于 esptool 自动识别） |

**CHIP_CTX eFuse 控制器字段（用于 C2/C3/C6 烧录模拟）：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `pgm_data[8]` | DWORD[] | PGM_DATA0-7 暂存区（烧录数据暂存） |
| `efuse_conf_ofs` | DWORD | CONF_REG 相对 EFUSE_BASE 的偏移（0 = 无控制器） |
| `efuse_cmd_ofs` | DWORD | CMD_REG 相对 EFUSE_BASE 的偏移 |

**函数：**

| 函数 | 说明 |
|------|------|
| `Chip_Init(ctx, type)` | 初始化芯片 |
| `Chip_Close(ctx)` | 释放芯片 |
| `Chip_GetName(ctx)` | 获取芯片名称 |
| `Chip_SetMac(ctx, mac)` | 设置MAC地址 |
| `Chip_GetMac(ctx)` | 获取MAC地址 |
| `Chip_ReadReg(ctx, addr)` | 读取寄存器（含 eFuse 值存储读取） |
| `Chip_WriteReg(ctx, addr, val)` | 写入寄存器（含 eFuse 控制器烧录模拟） |
| `Chip_SetFlashSize(ctx, size)` | 设置Flash大小 |
| `Chip_GetFlashSize(ctx)` | 获取Flash大小 |
| `Chip_GetChipId(ctx)` | 获取芯片ID |
| `Chip_GetEfuse(ctx)` | 获取efuse数据 |
| `Chip_GetEfuseSize(ctx)` | 获取efuse大小 |
| `Chip_GetBootBaudRate(ctx)` | 获取启动日志波特率 |
| `Chip_GetBootMessage(ctx, download_mode, reset_cause)` | 获取启动日志文本 |

**Chip_WriteReg eFuse 控制器行为：**
- 对于有控制器的芯片（C2/C3/C6，`efuse_conf_ofs != 0`）：
  - 写入 PGM_DATA 范围（`EFUSE_BASE+0x00..+0x1F`）→ 暂存到 `pgm_data[]`
  - 写入 CONF_REG → 记录解锁码
  - 写入 CMD_REG 且 bit1 置位 → 触发烧录：根据 block 编号将 `pgm_data[]` OR 写入 eFuse 数组的 block 位置
  - 其他寄存器写入 → 忽略
- 对于无控制器的芯片（ESP32/S2/S3）：直接 OR 写入 eFuse 数组

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

### deflate.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `DEFLATE_HUFF` | 霍夫曼编码表 |
| `DEFLATE_CTX` | 解压器上下文 |

**DEFLATE_HUFF 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `counts` | WORD* | 每个长度的编码数量 |
| `symbols` | WORD* | 按编码排序的符号表 |
| `max_length` | int | 最大编码长度 |

**DEFLATE_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `in_buf` | const BYTE* | 输入缓冲区（压缩数据） |
| `in_len` | size_t | 输入数据长度 |
| `in_pos` | size_t | 当前输入位置 |
| `out_buf` | BYTE* | 输出缓冲区（解压数据） |
| `out_len` | size_t | 输出缓冲区大小 |
| `out_pos` | size_t | 当前输出位置 |
| `bit_buf` | DWORD | 位缓冲区 |
| `bit_count` | int | 位缓冲区中的有效位数 |
| `lit_huff` | DEFLATE_HUFF | 字面量/长度霍夫曼编码 |
| `dist_huff` | DEFLATE_HUFF | 距离霍夫曼编码 |

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `DEFLATE_OK` | 0 | 成功 |
| `DEFLATE_ERROR` | -1 | 通用错误 |
| `DEFLATE_BAD_INPUT` | -2 | 输入数据无效 |
| `DEFLATE_NO_MEMORY` | -3 | 内存分配失败 |

**函数：**

| 函数 | 说明 |
|------|------|
| `deflate_init(ctx, in_buf, in_len, out_buf, out_len)` | 初始化解压器上下文 |
| `deflate_decompress(ctx)` | 执行 DEFLATE 解压 |

**使用示例：**

```c
#include "utils/deflate.h"

BYTE compressed[] = { /* ... */ };
BYTE decompressed[4096];
DEFLATE_CTX ctx;

deflate_init(&ctx, compressed, sizeof(compressed), decompressed, sizeof(decompressed));
int ret = deflate_decompress(&ctx);
if (ret == DEFLATE_OK) {
    // ctx.out_pos 包含解压后的数据长度
    // decompressed[0..ctx.out_pos-1] 包含解压后的数据
}
```

### encrypt.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `ENCRYPT_CTX` | 加密上下文 |

**ENCRYPT_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `key` | BYTE[64] | XTS 密钥（32 或 64 字节） |
| `key_len` | int | 密钥长度（32 或 64 字节） |
| `flash_addr` | DWORD | Flash 地址（用于 XTS tweak 计算） |

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `ENCRYPT_OK` | 0 | 成功 |
| `ENCRYPT_ERROR` | -1 | 通用错误 |
| `ENCRYPT_BAD_INPUT` | -2 | 输入数据无效 |
| `ENCRYPT_KEY_LEN_256` | 32 | 256-bit 密钥（XTS-AES-128） |
| `ENCRYPT_KEY_LEN_512` | 64 | 512-bit 密钥（XTS-AES-256） |
| `ENCRYPT_BLOCK_SIZE` | 128 | AES-XTS 块大小（1024 位） |

**函数：**

| 函数 | 说明 |
|------|------|
| `Encrypt_Init(ctx, key, key_len, flash_addr)` | 初始化加密上下文 |
| `Encrypt_Data(ctx, in_buf, out_buf, len)` | 加密数据 |
| `Decrypt_Data(ctx, in_buf, out_buf, len)` | 解密数据 |

**使用示例：**

```c
#include "utils/encrypt.h"

BYTE key[32] = { /* ... */ };
BYTE plaintext[4096];
BYTE ciphertext[4096];
ENCRYPT_CTX ctx;

Encrypt_Init(&ctx, key, 32, 0x10000);
Encrypt_Data(&ctx, plaintext, ciphertext, sizeof(plaintext));

// 解密
Decrypt_Data(&ctx, ciphertext, plaintext, sizeof(ciphertext));
```

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
| `DEVICE_CTX` | 设备上下文 |

**DEVICE_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `chip` | CHIP_CTX | 芯片特性（类型、MAC、eFuse、晶振频率） |
| `flash` | FLASH_CTX | Flash 存储（数据缓冲区、大小） |
| `filename` | WCHAR[MAX_PATH] | 当前文件路径 |
| `modified` | BOOL | 数据修改标记 |

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
| `Serial_EnumPorts(hCombo)` | 枚举可用串口到下拉框 |
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
| `Serial_GetPortName(index, portName, maxLen)` | 获取端口名（返回 BOOL） |
| `Serial_PostLog(hNotify, tag, text)` | 发送日志 |
| `Serial_PostLogF(hNotify, tag, fmt, ...)` | 格式化日志 |

**Listener 线程注意事项：**
- `Listener_Proc` 中不要使用过严的读取条件（如 `cbInQue < READ_BUFFER_SIZE`），应使用 `min(cbInQue, READ_BUFFER_SIZE)` 安全截断
- 避免在 listener 线程中同步调用 UI 函数（如 `SetWindowTextW`），应使用 `PostMessage` 异步通知
- stub 模式的 `FLASH_DEFL_DATA` 包经 SLIP 编码后约 16500 字节，`READ_BUFFER_SIZE` (32KB) 足以一次读取

**Serial_SetBaudRate 注意事项：**
- `FlushFileBuffers` 只保证数据到达 USB 串口芯片的内部 FIFO，不保证 FIFO 已物理发出
- 切换波特率前需等待 FIFO 排空，否则 FIFO 中剩余字节以错误波特率发送导致数据损坏
- 当前实现按 `256 * 10 / BaudRate` 计算延迟（256 字节 FIFO + 起止位），com0com 等虚拟串口不受影响

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
    DEVICE_CTX device;      /* 设备头信息 */
    BYTE *efuse;            /* eFuse 数据快照 */
    DWORD efuseSize;        /* eFuse 大小 */
    BYTE *flash;            /* Flash 数据快照 */
    DWORD flashSize;        /* Flash 大小 */
    WCHAR filename[MAX_PATH]; /* 输出文件名 */
    HWND hWnd;              /* 所有者窗口 */
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
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DENABLE_TRACE_PROTO=ON -DENABLE_TRACE_FW=ON -B build
cmake --build build --config Release -j
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

`ESPTOOL_CTX` 中新增以下字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `defl_buf` | BYTE* | 压缩数据积累缓冲区 |
| `defl_buf_size` | DWORD | 当前积累的压缩数据大小 |
| `defl_buf_cap` | DWORD | 缓冲区容量（等于 `uncompressed_size`） |
| `defl_offset` | DWORD | 当前 deflate 会话的 Flash 写入偏移 |
| `defl_unc_size` | DWORD | 当前 deflate 会话的未压缩大小 |

### 辅助函数

**`Defl_FreeBuffer(ctx)`**：释放积累缓冲区，不写入 flash。用于错误处理和资源清理。

**`Defl_FlushBuffer(ctx)`**：解压积累数据并写入 flash，然后释放缓冲区。返回 `ESP_OK` 或 `ESP_FAIL`。

### 函数修改说明

| 函数 | 修改内容 |
|------|----------|
| `Esptool_Init` | 初始化 `defl_buf = NULL`，`defl_buf_size = 0`，`defl_buf_cap = 0` |
| `Esptool_ResetState` | 调用 `Defl_FreeBuffer()`，重置所有 deflate 字段 |
| `HandleFlashDeflBegin` | 检查并处理上一次积累数据，分配新缓冲区 |
| `HandleFlashDeflData` | 积累数据到缓冲区，不立即解压 |
| `HandleFlashDeflEnd` | 调用 `Defl_FlushBuffer()` 解压并写入 |
| `HandleFlashBegin` | **不释放**缓冲区（等待后续 `FLASH_DEFL_END`） |
| `HandleFlashEnd` | 检查并 flush 未处理的缓冲区（ROM 模式场景） |
| `HandleEraseFlash` | 调用 `Defl_FreeBuffer()` 释放缓冲区 |
| `HandleEraseBlock` | 调用 `Defl_FreeBuffer()` 释放缓冲区 |

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
| `Esptool_ResetState` | Free | 硬件复位 |

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
