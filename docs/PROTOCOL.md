# FakeEsptool - esptool 协议文档

本文档描述 esptool 烧录协议的完整规范，用于核对设备端模拟实现。

---

## 1. SLIP 帧封装

### 1.1 帧定界

| 字节 | 常量名 | 含义 |
|------|--------|------|
| `0xC0` | SLIP_END | 帧开始 / 帧结束 |
| `0xDB` | SLIP_ESC | 转义符 |

每个 SLIP 帧以 `0xC0` 开始，以 `0xC0` 结束。

### 1.2 转义规则

| 原始字节 | 转义序列 | 常量名 |
|----------|----------|--------|
| `0xC0` | `0xDB 0xDC` | SLIP_ESC_END |
| `0xDB` | `0xDB 0xDD` | SLIP_ESC_ESC |
| 其他 | 原样传输 | - |

### 1.3 帧格式

```
[0xC0] [payload...] [0xC0]
```

payload 为转义后的数据。

**帧大小限制：**
- 最小帧：8 字节（请求/响应头）
- 最大帧：由 `bufferSize` 参数决定（默认 256 字节）
- 实际 payload 大小受 SLIP 转义影响

### 1.4 帧同步机制

连续接收到的 `0xC0` 视为空闲同步符，仅当 `0xC0` 后跟随非 `0xC0` 数据时才开始解析新帧。

**实现要点：**
- 串口流起始位置的 `0xC0` 应视为同步/对齐字节并丢弃
- 直到出现有效载荷数据才开始帧解析
- 这符合 RFC 1055 SLIP 规范

### 1.5 粘包与断帧处理

实际串口/USB 通信中，数据可能粘连或截断。

**实现要点：**
- 使用环形缓冲区暂存原始字节
- 按 `0xC0` 边界提取并执行反转义
- 完整还原 payload 后再解析协议头
- **严禁假设单次 `read()` 必返回完整帧**

### 1.6 帧校验

SLIP 协议本身不提供校验机制，校验由上层协议（数据包结构中的 Checksum 字段）负责。

---

## 2. 数据包结构

### 2.1 请求包（烧录器 → 设备）

```
+----------+--------+----------+-----------+----------+
| Direction| Command| Size     | Checksum  | Data     |
| 1 byte   | 1 byte | 2 bytes  | 4 bytes   | N bytes  |
+----------+--------+----------+-----------+----------+
| 0x00     | cmd    | size_lo  | chk[0]    | data[0]  |
|          |        | size_hi  | chk[1]    | data[1]  |
|          |        |          | chk[2]=0  | ...      |
|          |        |          | chk[3]=0  | data[N-1]|
+----------+--------+----------+-----------+----------+
```

**注意：**
- bytes 4-7 是 checksum（异或校验），由调用方计算并填入
- 仅最低 1 字节有效（XOR 校验结果），高 3 字节保留，填充 `0x00`

### 2.2 响应包（设备 → 烧录器）

```
+----------+--------+----------+-----------+----------+
| Direction| Command| Size     | Val       | Data     |
| 1 byte   | 1 byte | 2 bytes  | 4 bytes   | N bytes  |
+----------+--------+----------+-----------+----------+
| 0x01     | cmd    | size_lo  | val[0]    | data[0]  |
|          |        | size_hi  | val[1]    | data[1]  |
|          |        |          | val[2]    | ...      |
|          |        |          | val[3]    | data[N-1]|
+----------+--------+----------+-----------+----------+
```

**注意：**
- 响应包的 bytes 4-7 是 val 字段，含义因命令而异：
  - SYNC (0x08)：返回同步序列头 `07 07 12 20`（小端序 `0x20120707`）
  - READ_REG (0x0A)：返回寄存器值
  - 其他命令：返回请求的 checksum / value

### 2.3 字段说明

| 字段 | 说明 |
|------|------|
| Direction | `0x00` = 请求（烧录器→设备），`0x01` = 响应（设备→烧录器） |
| Command | 命令码 |
| Size | Data 字段的字节数（小端序） |
| Checksum | 请求包的校验和（仅请求包有） |
| Val | 响应包的值字段（返回请求的 checksum） |
| Data | 变长数据载荷 |

### 2.4 校验和计算

```c
BYTE checksum = 0xEF;
for (int i = 0; i < data_len; i++)
    checksum ^= data[i];
```

**注意：** 校验和结果仅 1 字节有效，填入 checksum 字段最低字节，高 3 字节填充 `0x00`。

### 2.5 响应数据中的 Status

大多数命令的响应包 Data 字段末尾包含状态码，用于指示命令执行结果。

**状态码长度因命令而异：**

| 命令 | Status 长度 | 说明 |
|------|-------------|------|
| FLASH_BEGIN (0x02) | 2 字节 | |
| FLASH_DATA (0x03) | 2 字节 | |
| FLASH_END (0x04) | 2 字节 | |
| MEM_BEGIN (0x05) | 2 字节 | |
| MEM_DATA (0x07) | 2 字节 | |
| MEM_END (0x06) | 2 字节 | |
| SYNC (0x08) | 4 字节 | |
| WRITE_REG (0x09) | 2 字节 | |
| READ_REG (0x0A) | 4 字节 | 寄存器值在 Val 字段 |
| CHANGE_BAUDRATE (0x0F) | 2 字节 | |
| FLASH_DEFL_BEGIN (0x10) | 2 字节 | |
| FLASH_DEFL_DATA (0x11) | 2 字节 | |
| FLASH_DEFL_END (0x12) | 2 字节 | |
| SPI_FLASH_MD5 (0x13) | 2 字节 | |
| GET_SECURITY_INFO (0x14) | 2 字节 | |
| ERASE_FLASH (0xD0) | 2 字节 | |
| ERASE_REGION (0xD1) | 2 字节 | |
| READ_FLASH (0xD2) | 2 字节 | |

**状态码格式：**
```
Data 字段: [...payload...][status_byte_1][status_byte_2]
```

| status_byte_1 | 含义 |
|---------------|------|
| 0x00 | 成功 |
| 非 0x00 | 失败 |

**注意：** 客户端检查 `data[N] != 0`（N 为 payload 长度）则认为命令失败。

**例外：** READ_REG (0x0A) 命令的响应 Data 返回 4 字节 status，寄存器值在 Val 字段。

---

## 3. 支持的命令

### 3.1 命令码表

| 码 | 名称 | ROM | Stub | 说明 |
|----|------|-----|------|------|
| 0x02 | FLASH_BEGIN | ✓ | ✓ | 开始 Flash 下载 |
| 0x03 | FLASH_DATA | ✓ | ✓ | Flash 下载数据 |
| 0x04 | FLASH_END | ✓ | ✓ | 结束 Flash 下载 |
| 0x05 | MEM_BEGIN | ✓ | ✓ | 开始内存下载 |
| 0x06 | MEM_END | ✓ | ✓ | 结束内存下载 |
| 0x07 | MEM_DATA | ✓ | ✓ | 内存下载数据 |
| 0x08 | SYNC | ✓ | ✓ | 同步握手 |
| 0x09 | WRITE_REG | ✓ | ✓ | 写寄存器 |
| 0x0A | READ_REG | ✓ | ✓ | 读寄存器 |
| 0x0F | CHANGE_BAUDRATE | ✓ | ✓ | 修改波特率 |
| 0x10 | FLASH_DEFL_BEGIN | ✓ | ✓ | 压缩 Flash 下载开始 |
| 0x11 | FLASH_DEFL_DATA | ✓ | ✓ | 压缩 Flash 下载数据 |
| 0x12 | FLASH_DEFL_END | ✓ | ✓ | 压缩 Flash 下载结束 |
| 0x13 | SPI_FLASH_MD5 | ✓ | ✓ | 计算 Flash MD5 |
| 0x14 | GET_SECURITY_INFO | ✓ | ✓ | 获取安全信息 |
| 0xD0 | ERASE_FLASH | ✗ | ✓ | 擦除整个 Flash |
| 0xD1 | ERASE_REGION | ✗ | ✓ | 擦除 Flash 区域 |
| 0xD2 | READ_FLASH | ✗ | ✓ | 读取 Flash |
| 0xD3 | RUN_USER_CODE | ✗ | ✓ | 运行用户代码（软复位） |

**注意：** 标记为 ✗ 的命令在该模式下不支持。ROM 收到不支持的命令会返回 `ROM_INVALID_RECV_MSG (0x05)`。

---

### 3.2 SYNC (0x08) - 同步握手

**请求：**
```
Direction: 0x00
Command:   0x08
Size:      0x24 0x00 (36 bytes)
Checksum:  <XOR of data>
Data:      0x07 0x07 0x12 0x20 55 55 55 ... 55 (36 bytes)
```

**响应：**
```
Direction: 0x01
Command:   0x08
Size:      0x04 0x00 (4 bytes)
Val:       0x07 0x07 0x12 0x20 (同步序列头 4 字节，小端序 0x20120707)
Data:      0x00 0x00 0x00 0x00 (status=成功)
```

**注意：** 真实设备在 Val 字段返回同步序列前 4 字节 `07 07 12 20`（小端序），而非请求的 checksum。esptool 客户端对此字段不做强校验。

**重复响应：** 真实设备收到 1 个 SYNC 请求后，连续发送 8 次相同的响应。

**兼容性说明：** 部分早期 Bootloader 仅返回 2 字节 `00 00`。若需兼容极老固件，可动态判断响应长度，>=4 字节时截取前 4 字节。

---

### 3.3 READ_REG (0x0A) - 读寄存器

**请求：**
```
Direction: 0x00
Command:   0x0A
Size:      0x04 0x00 (4 bytes)
Checksum:  <XOR of data>
Data:      addr[3:0] (寄存器地址，小端序)
```

**响应：**
```
Direction: 0x01
Command:   0x0A
Size:      0x04 0x00 (4 bytes)
Val:       寄存器值（小端序）
Data:      (空)
```

**注意：** READ_REG 是特例，寄存器值直接放在 Val 字段（bytes 4-7），Data 字段为空。与 PROTOCOL.md 原始描述不同，真实设备行为如此。

---

### 3.4 WRITE_REG (0x09) - 写寄存器

**请求：**
```
Direction: 0x00
Command:   0x09
Size:      0x10 0x00 (16 bytes)
Checksum:  <XOR of data>
Data:      [addr:4][value:4][mask:4][delay_us:4]
```

**响应：**
```
Direction: 0x01
Command:   0x09
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.5 CHANGE_BAUDRATE (0x0F) - 修改波特率

**请求：**
```
Direction: 0x00
Command:   0x0F
Size:      0x08 0x00 (8 bytes)
Checksum:  <XOR of data>
Data:      [new_baud:4][old_baud:4]
```

**响应（以旧波特率发送）：**

ESP32 及后续芯片（标准响应）：
```
Direction: 0x01
Command:   0x0F
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

ESP8266 ROM Bootloader（特例）：
```
Direction: 0x01
Command:   0x0F
Size:      0x08 0x00 (8 bytes)
Val:       <返回请求的 checksum>
Data:      [old_baud:4][new_baud:4]
```

**工程提示：** 模拟器建议统一按 ESP32 标准格式（2 字节 status）返回，ESP8266 客户端通常也能兼容。

**波特率切换时序：**
1. 主机发送请求（以旧波特率）
2. 设备以旧波特率响应
3. 主机收到响应后切换波特率
4. 后续通信使用新波特率

---

### 3.6 MEM_BEGIN (0x05) - 内存写入开始

**请求：**
```
Direction: 0x00
Command:   0x05
Size:      0x10 0x00 (16 bytes)
Checksum:  <XOR of data>
Data:      [total_size:4][blocks:4][block_size:4][offset:4]
```

**响应：**
```
Direction: 0x01
Command:   0x05
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.7 MEM_DATA (0x07) - 内存写入数据

**请求：**
```
Direction: 0x00
Command:   0x07
Size:      data_len + 16
Checksum:  <XOR of 完整 Data 字段>
Data:      [data_len:4][seq:4][padding:4][padding:4][payload:data_len]
```

**校验范围：** 覆盖 Size 声明的全部字节（含 4+4+4+4 字节头部及实际数据）。

**响应：**
```
Direction: 0x01
Command:   0x07
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

**工程提示：** 两个 `padding:4` 字段官方固定填充 `0x00 00 00 00`，模拟器直接 memset 即可。

---

### 3.8 MEM_END (0x06) - 内存写入结束

**请求：**
```
Direction: 0x00
Command:   0x06
Size:      0x08 0x00 (8 bytes)
Checksum:  <XOR of data>
Data:      [execute:4][entry_point:4]
```

**响应：**
```
Direction: 0x01
Command:   0x06
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.9 FLASH_BEGIN (0x02) - Flash 写入开始

**请求：**
```
Direction: 0x00
Command:   0x02
Size:      0x10 0x00 (16 bytes)
Checksum:  <XOR of data>
Data:      [erase_size:4][num_blocks:4][block_size:4][offset:4]
```

**响应：**
```
Direction: 0x01
Command:   0x02
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.10 FLASH_DATA (0x03) - Flash 写入数据

**请求：**
```
Direction: 0x00
Command:   0x03
Size:      data_len + 16
Checksum:  <XOR of 完整 Data 字段>
Data:      [data_len:4][seq:4][padding:4][padding:4][payload:data_len]
```

**校验范围：** 覆盖 Size 声明的全部字节（含 4+4+4+4 字节头部及实际数据）。

**响应：**
```
Direction: 0x01
Command:   0x03
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

**工程提示：** 两个 `padding:4` 字段官方固定填充 `0x00 00 00 00`，模拟器直接 memset 即可。

---

### 3.11 FLASH_END (0x04) - Flash 写入结束

**请求：**
```
Direction: 0x00
Command:   0x04
Size:      0x04 0x00 (4 bytes)
Checksum:  <XOR of data>
Data:      [reboot:4] (0=不重启, 1=重启)
```

**响应：**
```
Direction: 0x01
Command:   0x04
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.12 FLASH_DEFL_BEGIN (0x10) - 压缩写入开始

**请求：**
```
Direction: 0x00
Command:   0x10
Size:      0x10 0x00 (16 bytes)
Checksum:  <XOR of data>
Data:      [uncompressed_size:4][num_blocks:4][block_size:4][offset:4]
```

**响应：**
```
Direction: 0x01
Command:   0x10
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.13 FLASH_DEFL_DATA (0x11) - 压缩写入数据

**请求：**
```
Direction: 0x00
Command:   0x11
Size:      data_len + 16
Checksum:  <XOR of 完整 Data 字段>
Data:      [data_len:4][seq:4][padding:4][padding:4][compressed_data:data_len]
```

**校验范围：** 覆盖 Size 声明的全部字节（含 4+4+4+4 字节头部及实际数据）。

**响应：**
```
Direction: 0x01
Command:   0x11
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

**工程提示：** 两个 `padding:4` 字段官方固定填充 `0x00 00 00 00`，模拟器直接 memset 即可。

---

### 3.14 FLASH_DEFL_END (0x12) - 压缩写入结束

**请求：**
```
Direction: 0x00
Command:   0x12
Size:      0x04 0x00 (4 bytes)
Checksum:  <XOR of data>
Data:      [reboot:4] (0=不重启, 1=重启)
```

**响应：**
```
Direction: 0x01
Command:   0x12
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.15 SPI_FLASH_MD5 (0x13) - 计算 Flash MD5

**请求：**
```
Direction: 0x00
Command:   0x13
Size:      0x10 0x00 (16 bytes)
Checksum:  <XOR of data>
Data:      [addr:4][len:4][padding:8]
```

**响应：**
```
Direction: 0x01
Command:   0x13
Size:      0x22 0x00 (34 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功) + md5_hex[32] (32字节 ASCII 十六进制 MD5)
```

**工程提示：** MD5 返回的 32 字节是 ASCII 十六进制字符串（非二进制 MD5），模拟器需注意转换。

---

### 3.16 ERASE_FLASH (0xD0) - 擦除整个 Flash（stub）

**请求：**
```
Direction: 0x00
Command:   0xD0
Size:      0x00 0x00 (0 bytes)
Checksum:  0xEF
Data:      (无)
```

**响应：**
```
Direction: 0x01
Command:   0xD0
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

**工程提示：** 当 Size=0 时，Data 为空，Checksum 必须严格为初始值 `0xEF`。

---

### 3.17 ERASE_REGION (0xD1) - 擦除 Flash 区域（stub）

**请求：**
```
Direction: 0x00
Command:   0xD1
Size:      0x08 0x00 (8 bytes)
Checksum:  <XOR of data>
Data:      [offset:4][erase_len:4]
```

**响应：**
```
Direction: 0x01
Command:   0xD1
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.18 READ_FLASH (0xD2) - 读取 Flash（stub）

**请求：**
```
Direction: 0x00
Command:   0xD2
Size:      0x0C 0x00 (12 bytes)
Checksum:  <XOR of data>
Data:      [addr:4][read_len:4][block_size:4]
```

**响应：**
```
Direction: 0x01
Command:   0xD2
Size:      read_len + 2
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功) + flash_data[read_len]
```

---

### 3.19 GET_SECURITY_INFO (0x14) - 获取安全信息

**请求：**
```
Direction: 0x00
Command:   0x14
Size:      0x00 0x00 (0 bytes)
Checksum:  0xEF
Data:      (无)
```

**响应：**
```
Direction: 0x01
Command:   0x14
Size:      可变
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功) + security_info[N]
```

**security_info 结构（因芯片而异）：**

ESP32-S2（12 字节）：
```
[flags:4][flash_crypt_cnt:1][key_purposes:7]
```

ESP32-C3/S3 等（20 字节）：
```
[flags:4][flash_crypt_cnt:1][key_purposes:7][chip_id:4][api_version:4]
```

**最小兼容返回示例（12 字节）：**
```
Size: 0x0C 0x00
Data: 00 00 (status) + 00 00 00 00 (flags) + 00 (crypt_cnt) + 00 00 00 00 00 00 00 (key_purposes)
```

**工程提示：** 若仅需通过 esptool 基础检测，可固定返回上述 12 字节，避免客户端因长度不足抛出 `struct.error`。

---

## 4. 典型烧录流程

### 4.1 完整流程概览

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          完整烧录流程                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 连接与同步                                                              │
│     ├─ 重置芯片（DTR/RTS 信号）                                            │
│     └─ 发送 SYNC 命令，建立通信                                             │
│                                                                             │
│  2. 芯片检测                                                                │
│     ├─ READ_REG (0x40001000) 读取魔数                                       │
│     └─ 确定芯片类型                                                         │
│                                                                             │
│  3. 上传 Stub（可选）                                                       │
│     ├─ MEM_BEGIN + MEM_DATA + MEM_END 上传 text 段                          │
│     ├─ MEM_BEGIN + MEM_DATA + MEM_END 上传 data 段                          │
│     └─ 等待 "OHAI" 握手                                                     │
│                                                                             │
│  4. 修改波特率（可选）                                                      │
│     └─ CHANGE_BAUDRATE 切换到更高波特率                                     │
│                                                                             │
│  5. Flash 操作                                                              │
│     ├─ FLASH_DEFL_BEGIN 开始写入                                            │
│     ├─ FLASH_DEFL_DATA × N 写入数据                                         │
│     └─ FLASH_DEFL_END 结束写入                                              │
│                                                                             │
│  6. 验证（可选）                                                            │
│     └─ SPI_FLASH_MD5 计算 MD5 校验                                          │
│                                                                             │
│  7. 重置与重启                                                              │
│     └─ FLASH_END 或硬重置                                                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 连接与同步

```
烧录器 → 设备: SYNC (0x08) × 8 次
设备 → 烧录器: SYNC Response × 8 次 [val=0x07071220, data=00 00 00 00]
```

**重试机制：**
- 客户端发送 1 个 SYNC 请求
- 设备返回 8 个相同的响应
- 如果失败，客户端重试最多 7 次（每次重试前执行重置序列）

### 4.3 芯片检测

```
烧录器 → 设备: READ_REG (0x0A) [data=0x40001000]
设备 → 烧录器: READ_REG Response [val=魔数, data=status]
```

**魔数示例：**
- ESP32: `0x00F01D83`
- ESP32-S3: `0x00000009`
- ESP32-C3: `0x6921506F`

### 4.4 Stub 上传（可选但推荐）

Stub 上传后可获得更高效的 Flash 操作和额外功能。

**步骤 1：上传 text 段**
```
烧录器 → 设备: MEM_BEGIN (0x05) [size, blocks, block_size, text_start]
设备 → 烧录器: MEM_BEGIN Response

烧录器 → 设备: MEM_DATA (0x07) [len, seq=0, 0, 0, data]
设备 → 烧录器: MEM_DATA Response

烧录器 → 设备: MEM_DATA (0x07) [len, seq=1, 0, 0, data]
设备 → 烧录器: MEM_DATA Response

... (重复直到所有数据发送完毕)
```

**步骤 2：上传 data 段**
```
烧录器 → 设备: MEM_BEGIN (0x05) [size, blocks, block_size, data_start]
设备 → 烧录器: MEM_BEGIN Response

烧录器 → 设备: MEM_DATA (0x07) [len, seq=0, 0, 0, data]
设备 → 烧录器: MEM_DATA Response

... (重复直到所有数据发送完毕)
```

**步骤 3：执行 Stub**
```
烧录器 → 设备: MEM_END (0x06) [isEntry=1, entry_point]
设备 → 烧录器: MEM_END Response

设备 → 烧录器: "OHAI" (4 字节 ASCII)
```

### 4.5 修改波特率（可选）

```
烧录器 → 设备: CHANGE_BAUDRATE (0x0F) [new_baud, old_baud]
设备 → 烧录器: CHANGE_BAUDRATE Response

(双方切换到新波特率)
```

**时序说明：**
1. 主机以旧波特率发送请求
2. 设备以旧波特率发送响应
3. 主机收到响应后，双方同时切换到新波特率
4. 后续通信使用新波特率

### 4.6 Flash 烧录（压缩模式）

**步骤 1：开始写入**
```
烧录器 → 设备: FLASH_DEFL_BEGIN (0x10) [uncompressed_size, num_blocks, block_size, offset]
设备 → 烧录器: FLASH_DEFL_BEGIN Response
```

**步骤 2：写入数据**
```
烧录器 → 设备: FLASH_DEFL_DATA (0x11) [data_len, seq=0, 0, 0, compressed_data]
设备 → 烧录器: FLASH_DEFL_DATA Response

烧录器 → 设备: FLASH_DEFL_DATA (0x11) [data_len, seq=1, 0, 0, compressed_data]
设备 → 烧录器: FLASH_DEFL_DATA Response

... (重复直到所有数据发送完毕)
```

**步骤 3：结束写入（仅 Stub 模式需要）**
```
烧录器 → 设备: FLASH_DEFL_END (0x12) [reboot=0]
设备 → 烧录器: FLASH_DEFL_END Response
```

### 4.7 MD5 验证（可选）

```
烧录器 → 设备: SPI_FLASH_MD5 (0x13) [addr, len, 0, 0]
设备 → 烧录器: SPI_FLASH_MD5 Response [md5_hex(32 字节)]
```

**注意：**
- ROM 模式返回 32 字节 ASCII 十六进制 MD5
- Stub 模式返回 16 字节 ASCII 十六进制 MD5

### 4.8 重置与重启

**方式 1：软重置（FLASH_END）**
```
烧录器 → 设备: FLASH_END (0x04) [reboot=1]
设备 → 烧录器: FLASH_END Response
(设备重启)
```

**方式 2：硬重置（DTR/RTS 信号）**
```
烧录器: setRTS(false) → EN=HIGH → 芯片退出复位
```

### 4.9 典型时序图

```
时间轴 ─────────────────────────────────────────────────────────────────────►

     │ 连接 │ 芯片检测 │ Stub 上传 │ 波特率 │ Flash 写入 │ 验证 │ 重置 │
     ├──────┼─────────┼──────────┼────────┼───────────┼──────┼──────┤
     │      │         │          │        │           │      │      │
   SYNC   READ_REG  MEM_*    CHANGE   FLASH_DEFL  MD5    FLASH
                    ×N      BAUDRATE   ×N         END
```

### 4.10 错误恢复

**连接失败：**
1. 重试 SYNC（最多 7 次 × 5 次 = 35 次）
2. 尝试不同的重置序列（50ms / 550ms 延迟）

**命令失败：**
1. 检查 Status 字段
2. 重试命令（客户端负责）
3. 如果是 Stub 命令失败，尝试重新上传 Stub

---

## 5. 状态码

### 5.1 通用状态码

| 状态码 | 含义 | 说明 |
|--------|------|------|
| `0x00` | 成功 | 命令执行成功 |
| `非 0x00` | 失败 | 命令执行失败 |

### 5.2 ROM 错误码

当 ROM 收到不支持的命令时，返回特殊错误码：

| Data[1] | 含义 |
|---------|------|
| `0x05` | ROM_INVALID_RECV_MSG - 不支持的命令 |

**触发条件：** 调用 Stub 专属命令（0xD0-0xD3）时

**客户端处理：**
```typescript
if (data[0] != 0 && data[1] == 0x05) {
    throw new ESPError("unsupported command error");
}
```

### 5.3 常见失败原因

| 场景 | 可能原因 |
|------|----------|
| SYNC 失败 | 芯片未进入下载模式、串口连接问题 |
| READ_REG 失败 | 地址无效、芯片未初始化 |
| FLASH_BEGIN 失败 | 地址超出范围、大小无效 |
| FLASH_DATA 失败 | 校验和错误、序列号错误 |
| MEM_BEGIN 失败 | 地址与 Stub 冲突 |
| MD5 不匹配 | 写入数据损坏 |

---

## 6. 设备端行为规范

### 6.1 响应原则

- 设备端不主动发送数据，仅在收到完整有效请求后响应
- 若请求校验失败或命令未实现，直接丢弃或返回 Status != 0
- 由客户端负责重试和 fallback

### 6.2 超时与重试

esptool 客户端对 SYNC 采用多次短脉冲重试（默认 7 次，间隔 100ms）。

**设备端无需实现超时机制**，只需：
1. 正确解析收到的请求
2. 校验 checksum
3. 返回正确的响应

### 6.3 错误处理

| 情况 | 处理方式 |
|------|----------|
| 请求 checksum 错误 | 丢弃请求，不响应 |
| 命令未实现 | 返回 Status != 0 |
| 数据长度不匹配 | 丢弃请求，不响应 |
| 帧格式错误 | 丢弃请求，等待下一帧 |

### 6.4 SLIP 帧处理细节

**帧解析状态机：**
```
IDLE → WAIT_START → IN_PACKET → ESCAPING → PACKET_COMPLETE
```

**状态转换：**
- IDLE: 等待 0xC0 开始符
- WAIT_START: 收到 0xC0，等待下一个非 0xC0 字节
- IN_PACKET: 正在接收数据
- ESCAPING: 收到转义符 0xDB，等待下一个字节
- PACKET_COMPLETE: 收到 0xC0 结束符

**特殊情况：**
- 连续多个 0xC0：视为同步符，忽略
- 帧中间的 0xC0：视为结束符，开始新帧
- 转义序列错误：丢弃当前帧

### 6.5 Stub 模式行为

Stub 上传成功后，设备行为发生变化：

| 特性 | ROM 模式 | Stub 模式 |
|------|----------|-----------|
| OHAI 握手 | 不发送 | 发送 "OHAI" |
| SYNC 响应 Val | 非 0 | 0 |
| 支持命令 | 基础命令 | 所有命令 |
| 写入超时 | 设备写入后响应 | 收到数据后立即响应 |
| MD5 响应长度 | 32 字节 | 16 字节 |

---

## 7. 未实现的命令

以下命令在现代 esptool 中已较少使用，FakeEsptool 暂未实现：

| 命令码 | 名称 | 说明 |
|--------|------|------|
| 0x0D | SPI_ATTACH | SPI Flash 附加（已废弃，被 SPI_ATTACH_CMD 替代） |
| 0x0E | READ_FLASH_SLOW | 慢速读取 Flash（已废弃） |

**兼容性处理：** 若收到未实现的命令，可返回 Status != 0 或直接忽略，esptool 会 fallback。

**注意：** 0xD3 (RUN_USER_CODE) 虽然是 Stub 专属命令，但已在 esptool-js 中实现，用于软复位。

---

## 8. 参考资料

- [xingrz-esptool](https://github.com/xingrz/web-esptool) - TypeScript 实现
- [esptool-js](https://github.com/espressif/esptool-js) - JavaScript 实现
- [esptool 源码](https://github.com/espressif/esptool)
- [RFC 1055 - SLIP](https://tools.ietf.org/html/rfc1055)

---

## 附录 A：芯片识别

### A.1 识别机制

芯片通过读取地址 `0x40001000`（`CHIP_DETECT_MAGIC_REG_ADDR`）的魔数进行识别。该地址位于 DRAM 区域，上电后由硬件填充固定值。

### A.2 魔数映射表

| 魔数 | 芯片 | 说明 |
|------|------|------|
| `0x00F01D83` | ESP32 | |
| `0x000007C6` | ESP32-S2 | |
| `0x00000009` | ESP32-S3 | |
| `0x0C21E06F` | ESP32-C2 | |
| `0x6F51306F` | ESP32-C2 | 备选魔数 |
| `0x7C41A06F` | ESP32-C2 | 备选魔数 |
| `0x6921506F` | ESP32-C3 | |
| `0x1B31506F` | ESP32-C3 | 备选魔数 |
| `0x4881606F` | ESP32-C3 | 备选魔数 |
| `0x4361606F` | ESP32-C3 | 备选魔数 |
| `0x1101406F` | ESP32-C5 | |
| `0x63E1406F` | ESP32-C5 | 备选魔数 |
| `0x5FD1406F` | ESP32-C5 | 备选魔数 |
| `0x2CE0806F` | ESP32-C6 | |
| `0x2421606F` | ESP32-C61 | |
| `0x33F0206F` | ESP32-C61 | 备选魔数 |
| `0x4F81606F` | ESP32-C61 | 备选魔数 |
| `0xD7B73E80` | ESP32-H2 | |
| `0x97E30068` | ESP32-H2 | 备选魔数 |
| `0x00000000` | ESP32-P4 | |
| `0x0ADDBAD0` | ESP32-P4 | 备选魔数 |
| `0x07039AD9` | ESP32-P4 | 备选魔数 |
| `0xFFF0C101` | ESP8266 | |

### A.3 读取流程

```
1. 主机发送 READ_REG 请求，地址 = 0x40001000
2. 设备返回寄存器值（4 字节，小端序）
3. 主机根据魔数查表确定芯片类型
```

---

## 附录 B：MAC 地址读取

### B.1 概述

MAC 地址存储在 eFuse（一次性可编程存储器）中。不同芯片使用不同的读取方式：

- **ESP32/ESP8266**：通过 `EFUSE_RD_REG_BASE + offset` 间接读取
- **其他芯片**：通过专用 `MAC_EFUSE_REG` 寄存器直接读取

### B.2 MAC 地址存储格式

MAC 地址由 6 字节组成：`[OUI:3][NIC:3]`

```
OUI (Organizationally Unique Identifier): 厂商标识，前 3 字节
NIC (Network Interface Controller): 设备标识，后 3 字节
```

### B.3 ESP32 MAC 读取

**eFuse 基地址：** `0x3FF5A000`（`EFUSE_RD_REG_BASE`）

**读取地址：**
- `EFUSE_RD_REG_BASE + 4`（word1）→ 包含 MAC[2:5]
- `EFUSE_RD_REG_BASE + 8`（word2）→ 包含 MAC[0:1]

**字节映射：**
```
word1 (mac0): [byte5][byte4][byte3][byte2]
word2 (mac1): [byte1][byte0][unused:16]

MAC[0] = (mac1 >> 8) & 0xFF   // OUI byte 0
MAC[1] = mac1 & 0xFF          // OUI byte 1
MAC[2] = (mac0 >> 24) & 0xFF  // OUI byte 2
MAC[3] = (mac0 >> 16) & 0xFF  // NIC byte 0
MAC[4] = (mac0 >> 8) & 0xFF   // NIC byte 1
MAC[5] = mac0 & 0xFF          // NIC byte 2
```

**代码示例：**
```typescript
// 读取 ESP32 MAC
const mac0 = await readReg(0x3FF5A004);  // word1
const mac1 = await readReg(0x3FF5A008);  // word2
```

### B.4 ESP32-S2/S3, ESP32-C2/C3/C5/C6/C61/H2, ESP32-P4 MAC 读取

**通用模式：** 使用 `MAC_EFUSE_REG` 和 `MAC_EFUSE_REG + 4` 两个连续寄存器。

**字节映射：**
```
mac0 (MAC_EFUSE_REG):     [byte5][byte4][byte3][byte2]
mac1 (MAC_EFUSE_REG + 4): [0000...][byte1][byte0]

MAC[0] = (mac1 >> 8) & 0xFF   // OUI byte 0
MAC[1] = mac1 & 0xFF          // OUI byte 1
MAC[2] = (mac0 >> 24) & 0xFF  // OUI byte 2
MAC[3] = (mac0 >> 16) & 0xFF  // NIC byte 0
MAC[4] = (mac0 >> 8) & 0xFF   // NIC byte 1
MAC[5] = mac0 & 0xFF          // NIC byte 2
```

**各芯片 MAC_EFUSE_REG 地址：**

| 芯片 | EFUSE_BASE | MAC_EFUSE_REG | MAC_EFUSE_REG + 4 |
|------|------------|---------------|-------------------|
| ESP32-S2 | `0x3F41A000` | `0x3F41A044` | `0x3F41A048` |
| ESP32-S3 | `0x60007000` | `0x60007044` | `0x60007048` |
| ESP32-C2 | `0x60008800` | `0x60008840` | `0x60008844` |
| ESP32-C3 | `0x60008800` | `0x60008844` | `0x60008848` |
| ESP32-C5 | `0x600B4800` | `0x600B4844` | `0x600B4848` |
| ESP32-C6 | `0x600B0800` | `0x600B0844` | `0x600B0848` |
| ESP32-C61 | `0x600B4800` | `0x600B4844` | `0x600B4848` |
| ESP32-H2 | `0x600B0800` | `0x600B0844` | `0x600B0848` |
| ESP32-P4 | `0x5012D000` | `0x5012D044` | `0x5012D048` |

**注意：** ESP32-C2 的 `MAC_EFUSE_REG` 偏移为 `0x040`，比其他芯片的 `0x044` 少 4 字节。

### B.5 ESP8266 MAC 读取

**eFuse 基地址：** `0x3FF00050`（`EFUSE_RD_REG_BASE`）

**读取地址：**
- `EFUSE_RD_REG_BASE + 0`（word0）→ `0x3FF00050`
- `EFUSE_RD_REG_BASE + 4`（word1）→ `0x3FF00054`
- `EFUSE_RD_REG_BASE + 12`（word3）→ `0x3FF0005C`

**特殊逻辑：** ESP8266 使用 3 个 eFuse word，且 OUI 来源有三种情况：

```typescript
if (mac3 != 0) {
    // OUI 从 word3 获取
    OUI = [(mac3 >> 16) & 0xFF, (mac3 >> 8) & 0xFF, mac3 & 0xFF];
} else if (((mac1 >> 16) & 0xFF) == 0) {
    // 默认 OUI: Espressif
    OUI = [0x18, 0xFE, 0x34];
} else if (((mac1 >> 16) & 0xFF) == 1) {
    // 备选 OUI
    OUI = [0xAC, 0xD0, 0x74];
}

// NIC 部分
NIC = [(mac1 >> 8) & 0xFF, mac1 & 0xFF, (mac0 >> 24) & 0xFF];
```

---

## 附录 C：芯片版本检测

### C.1 概述

芯片版本（revision）通过 eFuse 中的特定位字段检测。不同芯片使用不同的 eFuse word 和位位置。

### C.2 ESP32 版本检测

**检测逻辑：** 组合 3 个不同来源的位

```
eFuse word3 bit[15] → revBit0
eFuse word5 bit[20] → revBit1
APB_CTL_DATE bit[31] → revBit2 (SYSCON_BASE + 0x7C = 0x3FF6607C)
```

**版本映射：**
| revBit0 | revBit1 | revBit2 | Revision |
|---------|---------|---------|----------|
| 0 | x | x | 0 |
| 1 | 0 | x | 1 |
| 1 | 1 | 0 | 2 |
| 1 | 1 | 1 | 3 |

**代码示例：**
```typescript
const word3 = await readEfuse(loader, 3);  // 0x3FF5A00C
const word5 = await readEfuse(loader, 5);  // 0x3FF5A014
const apbCtlDate = await readReg(0x3FF6607C);

const revBit0 = (word3 >> 15) & 0x1;
const revBit1 = (word5 >> 20) & 0x1;
const revBit2 = (apbCtlDate >> 31) & 0x1;
```

### C.3 ESP32-S2 版本检测

**使用两个独立函数：**

```typescript
// 主版本
async getMajorChipVersion(loader) {
    const word3 = await readReg(EFUSE_BASE + 0x30 + 4*3);  // BLOCK1 word3
    return (word3 >> 20) & 0x03;  // bits[21:20]
}

// 次版本
async getMinorChipVersion(loader) {
    const word3 = await readReg(EFUSE_BASE + 0x30 + 4*3);
    const word4 = await readReg(EFUSE_BASE + 0x30 + 4*4);
    return ((word3 >> 20) & 0x01) | ((word4 >> 4) & 0x07) << 1;  // 组合 bit
}
```

**eFuse 地址：** `0x3F41A044`（BLOCK1 + 0x00）

### C.4 ESP32-S3 版本检测

**特殊逻辑：** 有 `isEco0()` 检测用于 workaround

```typescript
// 主版本
async getMajorChipVersion(loader) {
    const word5 = await readReg(EFUSE_BASE + 0x44 + 4*5);  // BLOCK1 word5
    return (word5 >> 24) & 0x03;  // bits[25:24]
}

// 次版本
async getMinorChipVersion(loader) {
    const word5 = await readReg(EFUSE_BASE + 0x44 + 4*5);
    const word3 = await readReg(EFUSE_BASE + 0x44 + 4*3);
    return ((word5 >> 23) & 0x01) | (((word3 >> 18) & 0x07) << 1);
}
```

**eFuse 地址：** `0x60007044`（BLOCK1 + 0x00）

### C.5 ESP32-C2 版本检测

**简单 2 位字段：**

```typescript
async getChipRevision(loader) {
    const word1 = await readReg(EFUSE_BASE + 0x40 + 4*1);  // BLOCK1 word1
    return (word1 >> 20) & 0x03;  // bits[21:20]
}
```

**eFuse 地址：** `0x60008840`（BLOCK1 + 0x00）

### C.6 ESP32-C3 版本检测

**3 位字段：**

```typescript
async getChipRevision(loader) {
    const word3 = await readReg(EFUSE_BASE + 0x44 + 4*3);  // BLOCK1 word3
    return (word3 >> 18) & 0x07;  // bits[20:18]
}
```

**eFuse 地址：** `0x60008844`（BLOCK1 + 0x00）

### C.7 ESP32-C5 版本检测

**分离的 major/minor 字段：**

```typescript
async getMajorChipVersion(loader) {
    const word2 = await readReg(EFUSE_BASE + 0x30 + 4*2);  // BLOCK1 word2
    return (word2 >> 4) & 0x03;  // bits[5:4]
}

async getMinorChipVersion(loader) {
    const word2 = await readReg(EFUSE_BASE + 0x30 + 4*2);
    return word2 & 0x0F;  // bits[3:0]
}
```

**eFuse 地址：** `0x600B4844`（BLOCK1 + 0x00）

### C.8 ESP32-C6 版本检测

**3 位字段：**

```typescript
async getChipRevision(loader) {
    const word3 = await readReg(EFUSE_BASE + 0x44 + 4*3);  // BLOCK1 word3
    return (word3 >> 18) & 0x07;  // bits[20:18]
}
```

**eFuse 地址：** `0x600B0844`（BLOCK1 + 0x00）

### C.9 ESP32-C61 版本检测

**分离的 major/minor 字段：**

```typescript
async getMajorChipVersion(loader) {
    const word2 = await readReg(EFUSE_BASE + 0x30 + 4*2);  // BLOCK1 word2
    return (word2 >> 4) & 0x03;  // bits[5:4]
}

async getMinorChipVersion(loader) {
    const word2 = await readReg(EFUSE_BASE + 0x30 + 4*2);
    return word2 & 0x0F;  // bits[3:0]
}
```

**eFuse 地址：** `0x600B4844`（BLOCK1 + 0x00）

### C.10 ESP32-H2 版本检测

**分离的 major/minor 字段：**

```typescript
async getMajorChipVersion(loader) {
    const word3 = await readReg(EFUSE_BASE + 0x44 + 4*3);  // BLOCK1 word3
    return (word3 >> 21) & 0x03;  // bits[22:21]
}

async getMinorChipVersion(loader) {
    const word3 = await readReg(EFUSE_BASE + 0x44 + 4*3);
    return (word3 >> 18) & 0x07;  // bits[20:18]
}
```

**eFuse 地址：** `0x600B0844`（BLOCK1 + 0x00）

### C.11 ESP32-P4 版本检测

**特殊逻辑：** major 包含额外的高位 bit，返回 `major*100+minor`

```typescript
async getMajorChipVersion(loader) {
    const word2 = await readReg(EFUSE_BASE + 0x30 + 4*2);  // BLOCK1 word2
    return ((word2 >> 23) & 0x01) << 2 | ((word2 >> 4) & 0x03);  // bit[23]<<2 | bits[5:4]
}

async getMinorChipVersion(loader) {
    const word2 = await readReg(EFUSE_BASE + 0x30 + 4*2);
    return word2 & 0x0F;  // bits[3:0]
}

// 返回值示例：v0.1 → 1, v1.0 → 100, v2.3 → 203
```

**eFuse 地址：** `0x5012D044`（BLOCK1 + 0x00）

---

## 附录 D：Flash 配置

### D.1 Flash 大小映射

**通用映射（ESP32 系列）：**

| 大小 | 编码值 |
|------|--------|
| 1MB | `0x00` |
| 2MB | `0x10` |
| 4MB | `0x20` |
| 8MB | `0x30` |
| 16MB | `0x40` |
| 32MB | `0x50` |
| 64MB | `0x60` |
| 128MB | `0x70` |

**ESP8266 特殊映射：**

| 大小 | 编码值 |
|------|--------|
| 256KB | `0x10` |
| 512KB | `0x00` |
| 1MB | `0x20` |
| 2MB | `0x30` |
| 4MB | `0x40` |
| 2MB-c1 | `0x50` |
| 4MB-c1 | `0x60` |
| 8MB | `0x80` |
| 16MB | `0x90` |

### D.2 Flash 频率映射

**通用映射（大多数芯片）：**

| 频率 | 编码值 |
|------|--------|
| 80MHz | `0x0F` |
| 40MHz | `0x00` |
| 26MHz | `0x01` |
| 20MHz | `0x02` |

**ESP32-C5/C61 特殊映射（不支持 26MHz）：**

| 频率 | 编码值 |
|------|--------|
| 80MHz | `0x0F` |
| 40MHz | `0x00` |
| 20MHz | `0x02` |

### D.3 Flash 模式映射

| 模式 | 编码值 | 说明 |
|------|--------|------|
| QIO | `0x00` | Quad I/O |
| QOUT | `0x01` | Quad Output |
| DIO | `0x02` | Dual I/O |
| DOUT | `0x03` | Dual Output |

### D.4 Flash 参数存储位置

Flash 参数（模式、频率、大小）存储在 bootloader 镜像头部：

```
偏移 0x00: Magic byte (0xE9)
偏移 0x02: Flash mode (1 字节)
偏移 0x03: Flash size << 4 | Flash frequency (1 字节)
```

### D.5 Bootloader 偏移地址

| 芯片 | 偏移 |
|------|------|
| ESP32 | `0x1000` |
| ESP32-S2 | `0x1000` |
| ESP32-S3 | `0x0000` |
| ESP32-C2 | `0x0000` |
| ESP32-C3 | `0x0000` |
| ESP32-C5 | `0x2000` |
| ESP32-C6 | `0x0000` |
| ESP32-C61 | `0x0000` |
| ESP32-H2 | `0x0000` |
| ESP32-P4 | `0x2000` |
| ESP8266 | `0x0000` |

### D.6 Flash 写入块大小

| 芯片 | 块大小 |
|------|--------|
| ESP32 | `0x400` (1KB) |
| ESP32-S2 | `0x400` (1KB) |
| ESP32-S3 | `0x400` (1KB) |
| ESP32-C2 | `0x400` (1KB) |
| ESP32-C3 | `0x400` (1KB) |
| ESP32-C5 | `0x400` (1KB) |
| ESP32-C6 | `0x400` (1KB) |
| ESP32-C61 | `0x400` (1KB) |
| ESP32-H2 | `0x400` (1KB) |
| ESP32-P4 | `0x400` (1KB) |
| ESP8266 | `0x4000` (16KB) |

---

## 附录 E：SPI 寄存器

### E.1 SPI 寄存器基地址

| 芯片 | SPI_REG_BASE |
|------|--------------|
| ESP32 | `0x3FF42000` |
| ESP32-S2 | `0x3F402000` |
| ESP32-S3 | `0x60002000` |
| ESP32-C2 | `0x60002000` |
| ESP32-C3 | `0x60002000` |
| ESP32-C5 | `0x60002000` |
| ESP32-C6 | `0x60002000` |
| ESP32-C61 | `0x60002000` |
| ESP32-H2 | `0x60002000` |
| ESP32-P4 | `0x5008D000` |
| ESP8266 | `0x60000200` |

### E.2 SPI 寄存器偏移

**ESP32（独特布局）：**

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| SPI_USR | `0x1C` | 用户控制寄存器 |
| SPI_USR1 | `0x20` | 用户控制寄存器 1 |
| SPI_USR2 | `0x24` | 用户控制寄存器 2 |
| SPI_W0 | `0x80` | 数据寄存器 0 |
| SPI_MOSI_DLEN | `0x28` | MOSI 数据长度 |
| SPI_MISO_DLEN | `0x2C` | MISO 数据长度 |

**ESP32-S2/S3, ESP32-C2/C3/C5/C6/C61/H2, ESP32-P4（通用布局）：**

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| SPI_USR | `0x18` | 用户控制寄存器 |
| SPI_USR1 | `0x1C` | 用户控制寄存器 1 |
| SPI_USR2 | `0x20` | 用户控制寄存器 2 |
| SPI_W0 | `0x58` | 数据寄存器 0 |
| SPI_MOSI_DLEN | `0x24` | MOSI 数据长度 |
| SPI_MISO_DLEN | `0x28` | MISO 数据长度 |

**ESP8266：**

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| SPI_USR | `0x1C` | 用户控制寄存器 |
| SPI_USR1 | `0x20` | 用户控制寄存器 1 |
| SPI_USR2 | `0x24` | 用户控制寄存器 2 |
| SPI_W0 | `0x40` | 数据寄存器 0 |
| SPI_MOSI_DLEN | N/A | 不支持 |
| SPI_MISO_DLEN | N/A | 不支持 |

### E.3 SPI 命令寄存器位定义

```
SPI_USR 寄存器位：
  bit 31: SPI_USR_COMMAND  - 启用命令阶段
  bit 30: SPI_USR_ADDR     - 启用地址阶段
  bit 29: SPI_USR_DUMMY    - 启用 dummy 阶段
  bit 28: SPI_USR_MISO     - 启用读取阶段
  bit 27: SPI_USR_MOSI     - 启用写入阶段

SPI_USR2 寄存器位：
  bits[31:28]: SPI_USR2_COMMAND_LEN - 命令长度 - 1
  bits[15:0]:  SPI_USR2_COMMAND_VALUE - 命令值
```

---

## 附录 F：UART 寄存器

### F.1 UART 波特率分频寄存器

| 芯片 | UART_CLKDIV_REG | UART_CLKDIV_MASK |
|------|-----------------|------------------|
| ESP32 | `0x3FF40014` | `0xFFFFF` |
| ESP32-S2 | `0x3F400014` | `0xFFFFF` |
| ESP32-S3 | `0x60000014` | `0xFFFFF` |
| ESP32-C2 | `0x60000014` | `0xFFFFF` |
| ESP32-C3 | `0x3FF40014` | `0xFFFFF` |
| ESP32-C5 | `0x60000014` | `0xFFFFF` |
| ESP32-C6 | `0x3FF40014` | `0xFFFFF` |
| ESP32-C61 | `0x3FF40014` | `0xFFFFF` |
| ESP32-H2 | `0x3FF40014` | `0xFFFFF` |
| ESP32-P4 | `0x3FF40014` | `0xFFFFF` |
| ESP8266 | `0x60000014` | `0xFFFFF` |

### F.2 UART 日期寄存器

| 芯片 | UART_DATE_REG_ADDR | 说明 |
|------|-------------------|------|
| ESP32 | `0x60000078` | |
| ESP32-S2 | `0x60000078` | |
| ESP32-S3 | `0x60000080` | |
| ESP32-C2 | `0x6000007C` | |
| ESP32-C3 | `0x6000007C` | |
| ESP32-C5 | `0x6000007C` | |
| ESP32-C6 | `0x6000007C` | |
| ESP32-C61 | `0x6000007C` | |
| ESP32-H2 | `0x6000007C` | |
| ESP32-P4 | `0x500CA08C` | |
| ESP8266 | N/A | 不支持 |

---

## 附录 G：eFuse 基地址汇总

### G.1 EFUSE_BASE

| 芯片 | EFUSE_BASE | 说明 |
|------|------------|------|
| ESP32 | N/A | 使用 EFUSE_RD_REG_BASE |
| ESP32-S2 | `0x3F41A000` | |
| ESP32-S3 | `0x60007000` | |
| ESP32-C2 | `0x60008800` | |
| ESP32-C3 | `0x60008800` | |
| ESP32-C5 | `0x600B4800` | |
| ESP32-C6 | `0x600B0800` | |
| ESP32-C61 | `0x600B4800` | |
| ESP32-H2 | `0x600B0800` | |
| ESP32-P4 | `0x5012D000` | |
| ESP8266 | N/A | 使用 EFUSE_RD_REG_BASE |

### G.2 EFUSE_RD_REG_BASE

| 芯片 | EFUSE_RD_REG_BASE | 说明 |
|------|-------------------|------|
| ESP32 | `0x3FF5A000` | |
| ESP32-S2 | `0x3F41A030` | EFUSE_BASE + 0x30 |
| ESP32-S3 | `0x3FF5A000` | 继承自 ESP32（未使用） |
| ESP32-C2 | `0x3FF5A000` | 继承自 ESP32（未使用） |
| ESP32-C3 | `0x3FF5A000` | 继承自 ESP32（未使用） |
| ESP32-C5 | `0x600B4830` | EFUSE_BASE + 0x30 |
| ESP32-C6 | `0x3FF5A000` | 继承自 ESP32（未使用） |
| ESP32-C61 | `0x600B4830` | EFUSE_BASE + 0x30 |
| ESP32-H2 | `0x3FF5A000` | 继承自 ESP32（未使用） |
| ESP32-P4 | `0x5012D030` | EFUSE_BASE + 0x30 |
| ESP8266 | `0x3FF00050` | |

### G.3 BLOCK1 地址（版本检测用）

| 芯片 | BLOCK1 地址 |
|------|-------------|
| ESP32-S2 | `0x3F41A044` |
| ESP32-S3 | `0x60007044` |
| ESP32-C2 | `0x60008840` |
| ESP32-C3 | `0x60008844` |
| ESP32-C5 | `0x600B4844` |
| ESP32-C6 | `0x600B0844` |
| ESP32-C61 | `0x600B4844` |
| ESP32-H2 | `0x600B0844` |
| ESP32-P4 | `0x5012D044` |

---

## 附录 H：设备端模拟状态机

### H.1 连接状态机

```
                    ┌──────────────────────────────────────────────────────────────┐
                    │                                                              │
                    ▼                                                              │
              ┌──────────┐    SYNC OK     ┌──────────────┐    读取魔数    ┌────────────────┐
              │   IDLE   │ ──────────────►│  SYNC_DONE   │ ─────────────►│  CHIP_DETECTED │
              └──────────┘                └──────────────┘               └────────────────┘
                    │                           │                              │
                    │ SYNC 失败                 │ 读取魔数失败                  │
                    ▼                           ▼                              │
              ┌──────────┐                ┌──────────┐                         │
              │  ERROR   │                │  ERROR   │                         │
              └──────────┘                └──────────┘                         │
                                                                               │
                                                                               ▼
                    ┌──────────────────────────────────────────────────────────────────────┐
                    │                                                                      │
                    ▼                                                                      │
              ┌──────────────┐    MEM_END OK     ┌──────────────┐    OHAI OK    ┌────────────────┐
              │ STUB_LOADING │ ─────────────────►│  STUB_RUN    │ ─────────────►│   STUB_READY   │
              └──────────────┘                   └──────────────┘               └────────────────┘
                    │                                                                    │
                    │ MEM_END 失败                                                        │
                    ▼                                                                    │
              ┌──────────┐                                                              │
              │  ERROR   │                                                              │
              └──────────┘                                                              │
                                                                                        │
                    ┌──────────────────────────────────────────────────────────────────────┘
                    │
                    ▼
              ┌──────────────────────────────────────────────────────────────────┐
              │                          READY                                   │
              │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
              │  │ FLASH_WRITE │  │ FLASH_READ  │  │  ERASE      │              │
              │  └─────────────┘  └─────────────┘  └─────────────┘              │
              └──────────────────────────────────────────────────────────────────┘
```

### H.2 状态转换规则

| 当前状态 | 收到命令 | 下一状态 | 行为 |
|----------|----------|----------|------|
| IDLE | SYNC (0x08) | SYNC_DONE | 返回同步响应 |
| SYNC_DONE | READ_REG (0x0A) | CHIP_DETECTED | 返回芯片魔数 |
| CHIP_DETECTED | MEM_BEGIN (0x05) | STUB_LOADING | 准备接收 Stub |
| STUB_LOADING | MEM_DATA (0x07) | STUB_LOADING | 写入 Stub 数据块 |
| STUB_LOADING | MEM_END (0x06) | STUB_RUN | 执行 Stub 入口点 |
| STUB_RUN | (自动) | STUB_READY | 发送 "OHAI" |
| STUB_READY | 任意命令 | READY | 处理命令 |

---

## 附录 I：Stub 上传流程

### I.1 概述

Stub 是一段预编译的二进制代码，上传到芯片 RAM 后执行，提供更高效的 Flash 操作。Stub 模式相比 ROM 模式有以下优势：

- 支持全片擦除（`ERASE_FLASH`）
- 支持 Flash 读取（`READ_FLASH`）
- 更快的写入速度（先 ACK 再写入）
- 支持压缩传输

### I.2 上传流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Stub 上传流程                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 加载 Stub JSON                                                          │
│     └─ 根据芯片名和版本号加载对应的 stub_flasher_*.json                     │
│                                                                             │
│  2. 上传 text 段（代码段）                                                  │
│     ├─ MEM_BEGIN: size, blocks, block_size, text_start                      │
│     ├─ MEM_DATA (seq=0): 第一块数据                                         │
│     ├─ MEM_DATA (seq=1): 第二块数据                                         │
│     └─ ...                                                                  │
│                                                                             │
│  3. 上传 data 段（数据段）                                                  │
│     ├─ MEM_BEGIN: size, blocks, block_size, data_start                      │
│     ├─ MEM_DATA (seq=0): 第一块数据                                         │
│     └─ ...                                                                  │
│                                                                             │
│  4. 执行 Stub                                                               │
│     └─ MEM_END: isEntry=1, entry_point                                      │
│                                                                             │
│  5. 等待握手                                                                │
│     └─ 读取 4 字节 "OHAI"                                                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### I.3 Stub JSON 结构

```json
{
    "entry": 1074521580,           // 入口点地址 (0x400C1EEC)
    "text_start": 1074520064,      // text 段起始地址 (0x400C1900)
    "text": "<base64 encoded>",    // text 段数据 (Base64)
    "data_start": 1073605544,      // data 段起始地址 (0x3FFAE7A8)
    "data": "<base64 encoded>",    // data 段数据 (Base64)
    "bss_start": 1073528832        // BSS 段起始地址 (0x3FF9E000)
}
```

### I.4 OHAI 握手协议

Stub 启动后发送 4 字节 ASCII 字符串 `"OHAI"` 表示就绪：

```
设备 → 烧录器: [0x4F, 0x48, 0x41, 0x49]  ("OHAI")
```

**检测 Stub 是否已运行**：SYNC 响应的 Val 字段为 0 表示 Stub 模式，非 0 表示 ROM 模式。

### I.5 Stub 上传地址限制

- **text 段**：必须上传到 IRAM 地址空间
- **data 段**：必须上传到 DRAM 地址空间
- **块大小**：`ESP_RAM_BLOCK = 0x1800` (6KB)

**地址冲突检查**（Stub 模式下）：
```
如果加载地址与 Stub 驻留区域重叠，抛出错误：
- text 区域: [text_start, text_start + text_length]
- data 区域: [data_start, data_start + data_length]
```

---

## 附录 J：ROM vs Stub 命令对比

### J.1 命令支持矩阵

| 命令码 | 名称 | ROM 支持 | Stub 支持 | 说明 |
|--------|------|----------|-----------|------|
| 0x02 | FLASH_BEGIN | ✓ | ✓ | 开始 Flash 下载 |
| 0x03 | FLASH_DATA | ✓ | ✓ | Flash 下载数据 |
| 0x04 | FLASH_END | ✓ | ✓ | 结束 Flash 下载 |
| 0x05 | MEM_BEGIN | ✓ | ✓ | 开始内存下载 |
| 0x06 | MEM_END | ✓ | ✓ | 结束内存下载 |
| 0x07 | MEM_DATA | ✓ | ✓ | 内存下载数据 |
| 0x08 | SYNC | ✓ | ✓ | 同步握手 |
| 0x09 | WRITE_REG | ✓ | ✓ | 写寄存器 |
| 0x0A | READ_REG | ✓ | ✓ | 读寄存器 |
| 0x0F | CHANGE_BAUDRATE | ✓ | ✓ | 修改波特率 |
| 0x10 | FLASH_DEFL_BEGIN | ✓ | ✓ | 压缩写入开始 |
| 0x11 | FLASH_DEFL_DATA | ✓ | ✓ | 压缩写入数据 |
| 0x12 | FLASH_DEFL_END | ✓ | ✓ | 压缩写入结束 |
| 0x13 | SPI_FLASH_MD5 | ✓ | ✓ | 计算 Flash MD5 |
| 0x14 | GET_SECURITY_INFO | ✓ | ✓ | 获取安全信息 |
| 0xD0 | ERASE_FLASH | ✗ | ✓ | 擦除整个 Flash |
| 0xD1 | ERASE_REGION | ✗ | ✓ | 擦除 Flash 区域 |
| 0xD2 | READ_FLASH | ✗ | ✓ | 读取 Flash |
| 0xD3 | RUN_USER_CODE | ✗ | ✓ | 运行用户代码 |

### J.2 行为差异

#### J.2.1 flashBegin (0x02)

**ROM 模式**：
```
Data: [erase_size:4][num_blocks:4][block_size:4][offset:4][encrypted:4]
                                                           ^^^^^^^^^^^^
                                                           额外 4 字节
```

**Stub 模式**：
```
Data: [erase_size:4][num_blocks:4][block_size:4][offset:4]
                                                   无额外字节
```

#### J.2.2 flashDeflBegin (0x10)

**ROM 模式**（ESP32-S2/S3/C3/C2）：
```
Data: [uncompressed_size:4][num_blocks:4][block_size:4][offset:4][encrypted:4]
                                                                  ^^^^^^^^^^^^
                                                                  额外 4 字节
```

**Stub 模式**：
```
Data: [uncompressed_size:4][num_blocks:4][block_size:4][offset:4]
```

#### J.2.3 SPI_FLASH_MD5 (0x13)

**ROM 模式**：返回 32 字节 ASCII 十六进制 MD5
**Stub 模式**：返回 16 字节 ASCII 十六进制 MD5

#### J.2.4 CHANGE_BAUDRATE (0x0F)

**ROM 模式**：
```
Data: [new_baud:4][old_baud:4]
                   ^^^^^^^^^^
                   固定为 0
```

**Stub 模式**：
```
Data: [new_baud:4][old_baud:4]
                   ^^^^^^^^^^
                   ROM 波特率 (115200)
```

#### J.2.5 写入超时策略

**ROM 模式**：
```
1. 设置超时 = timeoutPerMb(ERASE_WRITE_TIMEOUT_PER_MB, block_size)
2. 发送 FLASH_DEFL_DATA
3. 等待响应（设备在写入 Flash 后才响应）
```

**Stub 模式**：
```
1. 发送 FLASH_DEFL_DATA
2. 等待响应（设备收到数据后立即响应）
3. 设置超时 = timeoutPerMb(ERASE_WRITE_TIMEOUT_PER_MB, block_size)
```

#### J.2.6 flashDeflFinish (0x12)

**ROM 模式**：不需要显式调用
**Stub 模式**：必须显式调用以完成写入

#### J.2.7 softReset

**ROM 模式**：
```
flashBegin(0, 0) + flashFinish(false)
```

**Stub 模式**（仅 ESP8266）：
```
command(ESP_RUN_USER_CODE, undefined, undefined, false)
```

---

## 附录 K：重置序列时序

### K.1 DTR/RTS 信号映射

| 信号 | 硬件引脚 | 说明 |
|------|----------|------|
| DTR=true | IO0=LOW | 进入下载模式 |
| DTR=false | IO0=HIGH | 退出下载模式 |
| RTS=true | EN=LOW | 芯片复位 |
| RTS=false | EN=HIGH | 芯片退出复位 |

### K.2 ClassicReset 时序

```
时间(ms)  0        100       100+delay
          |---------|---------|-------->
DTR:      LOW       HIGH      LOW
RTS:      HIGH      LOW       (保持)
EN:       LOW       HIGH      HIGH
IO0:      HIGH      LOW       HIGH

信号变化序列：
1. DTR=false, RTS=true   → EN=LOW (复位), IO0=HIGH
2. 等待 100ms
3. DTR=true, RTS=false   → IO0=LOW (下载模式), EN=HIGH (退出复位)
4. 等待 delay (50ms 或 550ms)
5. DTR=false             → IO0=HIGH (释放)
```

**重置延迟变体**：
- 第一次尝试：`delay = 50ms`
- 第二次尝试：`delay = 550ms`

### K.3 UsbJtagSerialReset 时序

```
时间(ms)  0    100   200        300
          |-----|-----|----------|-->
阶段1:  RTS=0, DTR=0   (稳定)
阶段2:  DTR=1, RTS=0   (IO0=LOW)
阶段3:  RTS=1, DTR=0, RTS=1  (脉冲)
阶段4:  RTS=0, DTR=0   (恢复)
```

### K.4 HardReset 时序

**普通模式**：
```
时间(ms)  0        100
          |---------|-------->
RTS:      (保持)    LOW → HIGH
```

**USB-OTG 模式**：
```
时间(ms)  0    200       400
          |-----|---------|-->
RTS:      (保持) LOW      (保持)
```

### K.5 CustomReset 格式

序列字符串格式：`"D0|R1|W100|D1|R0|W50|D0"`

| 命令 | 参数 | 说明 |
|------|------|------|
| D | 0 或 1 | setDTR |
| R | 0 或 1 | setRTS |
| W | 正整数 | 等待毫秒数 |

**验证规则**：
- 命令必须是 D、R、W 之一
- D/R 参数必须是 0 或 1
- W 参数必须是正整数

### K.6 连接重试逻辑

```
外层循环: attempts = 7 次
  └─ 内层循环: sync retries = 5 次
      └─ 每次重试:
          1. 执行重置序列
          2. flushInput()
          3. 发送 SYNC
          4. 等待响应
```

**重置策略交替**：
- 奇数次：ClassicReset (delay=50ms)
- 偶数次：ClassicReset (delay=550ms)

**总重试次数**：7 × 5 = 35 次

---

## 附录 L：镜像格式

### L.1 公共头部（8 字节）

```
偏移   大小   含义
0x00   1B    magic (0xe9)
0x01   1B    segment count
0x02   1B    flash_mode: qio=0, qout=1, dio=2, dout=3
0x03   1B    flash_size(高4位) + flash_freq(低4位)
0x04   4B    entry point address (little-endian)
```

### L.2 ESP32 扩展头部（16 字节，偏移 0x08）

```
偏移   大小   含义
0x08   1B    wp_pin
0x09   1B    clk_drv(低4) | q_drv(高4)
0x0A   1B    d_drv(低4)  | cs_drv(高4)
0x0B   1B    hd_drv(低4) | wp_drv(高4)
0x0C   1B    chip_id
0x0D   1B    min_rev
0x0E   2B    min_rev_full (LE)
0x10   2B    max_rev_full (LE)
0x11-0x16  6B  保留(零填充)
0x17   1B    append_digest (0 或 1)
```

### L.3 Segment 结构

```
偏移   大小   含义
0x00   4B    load address (little-endian)
0x04   4B    data length (little-endian)
0x08   NB    segment data
```

### L.4 校验和

**算法**：
```c
checksum = 0xEF;  // ESP_CHECKSUM_MAGIC
for (each byte in all segments) {
    checksum ^= byte;
}
```

**存放位置**：所有 segment 之后，16 字节对齐的位置

### L.5 Flash 参数编码

**偏移 0x03**：
```
高 4 位: flash_size
  0x0 = 1MB, 0x1 = 2MB, 0x2 = 4MB, 0x3 = 8MB, ...

低 4 位: flash_freq
  0x0 = 40MHz, 0x1 = 26MHz, 0x2 = 20MHz, 0xF = 80MHz
```

---

## 附录 M：内存地址空间映射

### M.1 各芯片地址范围汇总

#### ESP32

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x40080000 | 0x400A0000 | 指令 RAM |
| DRAM | 0x3FFAE000 | 0x40000000 | 数据 RAM |
| IROM | 0x400D0000 | 0x40400000 | Flash 指令映射 |
| DROM | 0x3F400000 | 0x3F800000 | Flash 数据映射 |

#### ESP32-S2

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x40020000 | 0x40070000 | 指令 RAM |
| DRAM | 0x3FFB0000 | 0x40000000 | 数据 RAM |
| IROM | 0x40080000 | 0x40800000 | Flash 指令映射 |
| DROM | 0x3F000000 | 0x3FF80000 | Flash 数据映射 |

#### ESP32-S3

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x40370000 | 0x403E0000 | 指令 RAM |
| DRAM | 0x3FC88000 | 0x3FD00000 | 数据 RAM |
| IROM | 0x42000000 | 0x42800000 | Flash 指令映射 |
| DROM | 0x3C000000 | 0x3D000000 | Flash 数据映射 |

#### ESP32-C3

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x4037C000 | 0x403E0000 | 指令 RAM |
| DRAM | 0x3FC80000 | 0x3FCE0000 | 数据 RAM |
| IROM | 0x42000000 | 0x42800000 | Flash 指令映射 |
| DROM | 0x3C000000 | 0x3C800000 | Flash 数据映射 |

#### ESP32-C2

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x4037C000 | 0x403C0000 | 指令 RAM |
| DRAM | 0x3FCA0000 | 0x3FCE0000 | 数据 RAM |
| IROM | 0x42000000 | 0x42400000 | Flash 指令映射 |
| DROM | 0x3C000000 | 0x3C400000 | Flash 数据映射 |

#### ESP32-C6

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x40800000 | 0x40880000 | 指令 RAM |
| DRAM | 0x40800000 | 0x40880000 | 数据 RAM |
| IROM | 0x42000000 | 0x43000000 | Flash 指令映射 |
| DROM | 0x42000000 | 0x43000000 | Flash 数据映射 |

#### ESP32-H2

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x40800000 | 0x40880000 | 指令 RAM |
| DRAM | 0x40800000 | 0x40880000 | 数据 RAM |
| IROM | 0x42000000 | 0x43000000 | Flash 指令映射 |
| DROM | 0x42000000 | 0x43000000 | Flash 数据映射 |

#### ESP32-P4

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x4FF00000 | 0x4FFA0000 | 指令 RAM |
| DRAM | 0x4FF00000 | 0x4FFA0000 | 数据 RAM |
| IROM | 0x40000000 | 0x4C000000 | Flash 指令映射 |
| DROM | 0x40000000 | 0x4C000000 | Flash 数据映射 |

#### ESP8266

| 区域 | 起始地址 | 结束地址 | 说明 |
|------|----------|----------|------|
| IRAM | 0x40100000 | 0x40108000 | 指令 RAM |
| DRAM | 0x3FFE8000 | 0x40000000 | 数据 RAM |
| IROM | 0x40200000 | 0x40300000 | Flash 指令映射 |

### M.2 Stub 上传地址验证

**合法地址范围**：
- text 段：必须在 IRAM 地址范围内
- data 段：必须在 DRAM 地址范围内

**地址冲突检查**（Stub 模式下）：
```
检查区域：
  [text_start, text_start + text_length]
  [data_start, data_start + data_length]

如果新加载地址与上述区域重叠，返回错误
```

---

## 附录 N：SPI Flash 命令详解

### N.1 SPI 寄存器位定义

#### SPI_USR 寄存器

| Bit | 名称 | 说明 |
|-----|------|------|
| 31 | SPI_USR_COMMAND | 启用命令阶段 |
| 30 | SPI_USR_ADDR | 启用地址阶段 |
| 29 | SPI_USR_DUMMY | 启用 dummy 阶段 |
| 28 | SPI_USR_MISO | 启用读数据阶段 |
| 27 | SPI_USR_MOSI | 启用写数据阶段 |

#### SPI_USR2 寄存器

| Bit | 名称 | 说明 |
|-----|------|------|
| 31:28 | SPI_USR2_COMMAND_LEN | 命令长度 - 1 |
| 15:0 | SPI_USR2_COMMAND_VALUE | 命令值 |

#### SPI_USR1 寄存器

| Bit | 名称 | 说明 |
|-----|------|------|
| 31:26 | SPI_USR_ADDR_LEN | 地址长度 - 1 |
| 4:0 | SPI_USR_DUMMY_LEN | dummy 长度 - 1 |

### N.2 Flash ID 读取

**SPI 命令**：`0x9F` (JEDEC Read ID)

**读取参数**：
- MOSI：无
- MISO：24 bits (3 bytes)

**返回值解析**：
```
bits [7:0]    = Manufacturer ID
bits [15:8]   = Device ID (高字节)
bits [23:16]  = Device ID (低字节，容量标识)
```

**常见 Manufacturer ID**：
| ID | 厂商 |
|----|------|
| 0xE0 | Espressif |
| 0x20 | XMC |
| 0xC8 | GigaDevice |
| 0xEF | Winbond |
| 0xC2 | Macronix |

### N.3 Flash 容量标识

| 标识字节 | 容量 |
|----------|------|
| 0x12 | 256KB |
| 0x13 | 512KB |
| 0x14 | 1MB |
| 0x15 | 2MB |
| 0x16 | 4MB |
| 0x17 | 8MB |
| 0x18 | 16MB |
| 0x19 | 32MB |
| 0x1A | 64MB |
| 0x1B | 128MB |
| 0x1C | 256MB |

### N.4 SPI 命令执行流程

```
1. 保存当前 SPI_USR 和 SPI_USR2 寄存器值
2. 计算 flags (启用哪些阶段)
3. 设置数据长度 (SPI_MOSI_DLEN / SPI_MISO_DLEN)
4. 写 SPI_USR_REG (flags)
5. 写 SPI_USR2_REG (命令长度 | 命令值)
6. 写 SPI_ADDR_REG (如果有地址)
7. 写数据到 SPI_W0_REG (如果有数据)
8. 写 SPI_CMD_REG 触发执行
9. 轮询 SPI_CMD_REG 直到 bit 清零
10. 读取结果从 SPI_W0_REG
11. 恢复 SPI_USR 和 SPI_USR2 寄存器
```

### N.5 SPI_ADDR_REG_MSB 说明

| 芯片 | SPI_ADDR_REG_MSB | 地址处理 |
|------|------------------|----------|
| ESP32 | true | 左对齐：`addr << (32 - addrLen)` |
| ESP32-S2 | false | 直接写入 |
| ESP32-S3 | false | 直接写入 |
| ESP32-C* | false | 直接写入 |
| ESP8266 | true | 左对齐 |

---

## 附录 O：错误响应详解

### O.1 ROM_INVALID_RECV_MSG (0x05)

当 ROM 收到不支持的命令时返回：

```
响应 Data 字段: [status_hi=0x00][status_lo=0x05]
```

**触发条件**：
- 调用 Stub 专属命令（0xD0-0xD3）时
- ROM 不支持的扩展命令

**客户端处理**：
```typescript
if (data[0] != 0 && data[1] == 0x05) {
    throw new ESPError("unsupported command error");
}
```

### O.2 状态码格式

**Data 字段布局**：
```
[data_payload(N bytes)][status_byte_1][status_byte_2]
```

**状态码含义**：
| status_byte_1 | status_byte_2 | 含义 |
|---------------|---------------|------|
| 0x00 | 0x00 | 成功 |
| 0x01 | - | 通用错误 |
| 0x02 | - | 数据长度错误 |
| 0x03 | - | 命令格式错误 |
| 0x04 | - | 校验和错误 |
| 0x05 | - | 命令不支持 |

### O.3 ESPError 触发条件汇总

| 位置 | 条件 | 错误消息 |
|------|------|----------|
| readPacket | 收到 0x05 | "unsupported command error" |
| readPacket | 100 次读取无响应 | "invalid response" |
| checkCommand | 响应数据过短 | "Only got N bytes of data" |
| checkCommand | 状态字节非零 | "failed with status [x,y]" |
| runSpiflashCommand | 读取 >32 bits | "Reading more than 32 bits..." |
| runSpiflashCommand | 写入 >64 bytes | "Writing more than 64 bytes..." |
| runSpiflashCommand | SPI 超时 | "SPI command did not complete..." |
| connect | 7 次连接失败 | "Failed to connect with the device" |
| memBegin | 地址重叠 | "Software loader is resident at..." |
| writeFlash | 超出容量 | "File doesn't fit in the available flash" |
