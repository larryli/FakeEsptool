# FakeEsptool - esptool 协议文档

本文档描述 esptool 烧录协议的完整规范，用于核对设备端模拟实现。

---

## 1. SLIP 帧封装

### 1.1 帧定界

| 字节 | 含义 |
|------|------|
| `0xC0` | 帧开始 / 帧结束 |

每个 SLIP 帧以 `0xC0` 开始，以 `0xC0` 结束。

### 1.2 转义规则

| 原始字节 | 转义序列 |
|----------|----------|
| `0xC0` | `0xDB 0xDC` |
| `0xDB` | `0xDB 0xDD` |
| 其他 | 原样传输 |

### 1.3 帧格式

```
[0xC0] [payload...] [0xC0]
```

payload 为转义后的数据。

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
- 响应包的 bytes 4-7 是 val 字段，应返回请求包的 checksum 值
- Val 字段为小端序 32 位整数，低字节为 checksum 值，高 3 字节固定填充 `0x00`，与 2.1 节严格对称

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

大多数命令的响应包 Data 字段前 2 字节是 status：

| 字节 | 含义 |
|------|------|
| 0x00 | 成功 |
| 非0 | 失败 |

客户端检查 `data[0] != 0` 则认为命令失败。

**例外：** READ_REG (0x0A) 命令的响应 Data 直接返回 4 字节寄存器值，无 status 前缀。

---

## 3. 支持的命令

### 3.1 命令码表

| 码 | 名称 | 说明 |
|----|------|------|
| 0x02 | FLASH_BEGIN | 开始 Flash 下载 |
| 0x03 | FLASH_DATA | Flash 下载数据 |
| 0x04 | FLASH_END | 结束 Flash 下载 |
| 0x05 | MEM_BEGIN | 开始内存下载 |
| 0x06 | MEM_END | 结束内存下载 |
| 0x07 | MEM_DATA | 内存下载数据 |
| 0x08 | SYNC | 同步握手 |
| 0x09 | WRITE_REG | 写寄存器 |
| 0x0A | READ_REG | 读寄存器 |
| 0x0F | CHANGE_BAUDRATE | 修改波特率 |
| 0x10 | FLASH_DEFL_BEGIN | 压缩 Flash 下载开始 |
| 0x11 | FLASH_DEFL_DATA | 压缩 Flash 下载数据 |
| 0x12 | FLASH_DEFL_END | 压缩 Flash 下载结束 |
| 0x13 | SPI_FLASH_MD5 | 计算 Flash MD5 |
| 0x14 | GET_SECURITY_INFO | 获取安全信息 |
| 0xD0 | ERASE_FLASH | 擦除整个 Flash（stub） |
| 0xD1 | ERASE_REGION | 擦除 Flash 区域（stub） |
| 0xD2 | READ_FLASH | 读取 Flash（stub） |

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
Val:       <返回请求的 checksum>
Data:      0x00 0x00 0x00 0x00 (status=成功)
```

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
Val:       <返回请求的 checksum>
Data:      val[3:0] (寄存器值，小端序)
```

**注意：** READ_REG 是特例，响应 Data 直接返回 4 字节寄存器值，无 status 前缀。

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

### 4.1 连接与同步

```
烧录器 → 设备: SYNC (0x08)
设备 → 烧录器: SYNC Response [val=checksum, data=00 00 00 00]
```

### 4.2 读取芯片信息

```
烧录器 → 设备: READ_REG (0x0A) [data=0x3FF5A000]
设备 → 烧录器: READ_REG Response [val=checksum, data=chip_id]
```

### 4.3 修改波特率

```
烧录器 → 设备: CHANGE_BAUDRATE (0x0F) [data=new_baud,old_baud]
设备 → 烧录器: CHANGE_BAUDRATE Response [val=checksum]
  (双方切换到新波特率)
```

**时序说明：**
1. 主机以旧波特率发送请求
2. 设备以旧波特率发送响应
3. 主机收到响应后，双方同时切换到新波特率
4. 后续通信使用新波特率

### 4.4 Flash 烧录（压缩）

```
烧录器 → 设备: FLASH_DEFL_BEGIN (0x10) [data=erase_size,num_blocks,block_size,offset]
设备 → 烧录器: FLASH_DEFL_BEGIN Response

烧录器 → 设备: FLASH_DEFL_DATA (0x11) [data=data_len,seq,0,0,compressed_data]
设备 → 烧录器: FLASH_DEFL_DATA Response

... (重复直到所有数据发送完毕)

烧录器 → 设备: FLASH_DEFL_END (0x12) [data=reboot_flag]
设备 → 烧录器: FLASH_DEFL_END Response
```

---

## 5. 状态码

| 状态码 | 含义 |
|--------|------|
| `0x0000` | 成功 |
| `非0` | 失败 |

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

---

## 7. 未实现的命令

以下命令在现代 esptool 中已较少使用，FakeEsptool 暂未实现：

| 命令码 | 名称 | 说明 |
|--------|------|------|
| 0x0D | FLASH_ATTACH | Flash 附加（已废弃） |
| 0x0E | READ_FLASH_SLOW | 慢速读取 Flash（已废弃） |

**兼容性处理：** 若收到未实现的命令，可返回 Status != 0 或直接忽略，esptool 会 fallback。

---

## 8. 参考资料

- [xingrz-esptool](https://github.com/xingrz/web-esptool) - TypeScript 实现
- [esptool-js](https://github.com/espressif/esptool-js) - JavaScript 实现
- [esptool 源码](https://github.com/espressif/esptool)
- [RFC 1055 - SLIP](https://tools.ietf.org/html/rfc1055)
