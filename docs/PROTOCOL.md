# FakeEsptool - esptool 协议文档

本文档描述 esptool 烧录协议的完整规范，用于核对设备端模拟实现。

参考实现：`D:\larryli\xingrz-esptool`

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
|          |        |          | chk[2]    | ...      |
|          |        |          | chk[3]    | data[N-1]|
+----------+--------+----------+-----------+----------+
```

**注意：** bytes 4-7 是 checksum（异或校验），不是 0！

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

**注意：** 响应包的 bytes 4-7 是 val 字段，应返回请求包的 checksum 值。

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

### 2.5 响应数据中的 Status

响应包的 Data 字段前 2 字节是 status：

| 字节 | 含义 |
|------|------|
| 0x00 | 成功 |
| 非0 | 失败 |

客户端检查 `data[0] != 0` 则认为命令失败。

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

---

### 3.3 READ_REG (0x0A) - 读寄存器

**请求：**
```
Direction: 0x00
Command:   0x0A
Size:      0x00 0x00 (0 bytes)
Checksum:  0xEF (固定)
Val:       addr[3:0] (寄存器地址，小端序)
```

**响应：**
```
Direction: 0x01
Command:   0x0A
Size:      0x04 0x00 (4 bytes)
Val:       <返回请求的 checksum>
Data:      val[3:0] (寄存器值，小端序)
```

---

### 3.4 WRITE_REG (0x09) - 写寄存器

**请求：**
```
Direction: 0x00
Command:   0x09
Size:      0x04 0x00 (4 bytes)
Checksum:  <XOR of data>
Val:       addr[3:0] (寄存器地址)
Data:      val[3:0] (写入值)
```

**响应：**
```
Direction: 0x01
Command:   0x09
Size:      0x00 0x00 (0 bytes)
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
Val:       0x00 0x00 0x00 0x00
Data:      [new_baud:4][old_baud:4]
```

**响应（以旧波特率发送）：**
```
Direction: 0x01
Command:   0x0F
Size:      0x08 0x00 (8 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.6 MEM_BEGIN (0x05) - 内存写入开始

**请求：**
```
Direction: 0x00
Command:   0x05
Size:      0x10 0x00 (16 bytes)
Checksum:  <XOR of data>
Val:       total_size[3:0] (总大小)
Data:      [blocks:4][block_size:4][offset:4]
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
Checksum:  <XOR of data>
Val:       seq[3:0] (序列号)
Data:      [data_len:4][seq:4][padding:4][padding:4][payload:data_len]
```

**响应：**
```
Direction: 0x01
Command:   0x07
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.8 MEM_END (0x06) - 内存写入结束

**请求：**
```
Direction: 0x00
Command:   0x06
Size:      0x08 0x00 (8 bytes)
Checksum:  <XOR of data>
Val:       execute[3:0] (0=不执行, 1=执行)
Data:      entry_point[3:0] (入口地址)
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
Val:       erase_size[3:0] (擦除大小)
Data:      [num_blocks:4][block_size:4][offset:4]
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
Checksum:  <XOR of payload data>
Val:       seq[3:0] (序列号)
Data:      [data_len:4][seq:4][padding:4][padding:4][payload:data_len]
```

**响应：**
```
Direction: 0x01
Command:   0x03
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.11 FLASH_END (0x04) - Flash 写入结束

**请求：**
```
Direction: 0x00
Command:   0x04
Size:      0x04 0x00 (4 bytes)
Checksum:  <XOR of data>
Val:       reboot[3:0] (0=不重启, 1=重启)
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
Val:       uncompressed_size[3:0] (解压后大小)
Data:      [num_blocks:4][block_size:4][offset:4]
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
Checksum:  <XOR of payload data>
Val:       seq[3:0] (序列号)
Data:      [data_len:4][seq:4][padding:4][padding:4][compressed_data:data_len]
```

**响应：**
```
Direction: 0x01
Command:   0x11
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.14 FLASH_DEFL_END (0x12) - 压缩写入结束

**请求：**
```
Direction: 0x00
Command:   0x12
Size:      0x04 0x00 (4 bytes)
Checksum:  <XOR of data>
Val:       reboot[3:0] (0=不重启, 1=重启)
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
Val:       addr[3:0] (Flash 地址)
Data:      [len:4][padding:8]
```

**响应：**
```
Direction: 0x01
Command:   0x13
Size:      0x22 0x00 (34 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功) + md5_hex[32] (32字节 ASCII 十六进制 MD5)
```

---

### 3.16 ERASE_FLASH (0xD0) - 擦除整个 Flash（stub）

**请求：**
```
Direction: 0x00
Command:   0xD0
Size:      0x00 0x00 (0 bytes)
Checksum:  0xEF
Val:       0x00 0x00 0x00 0x00
```

**响应：**
```
Direction: 0x01
Command:   0xD0
Size:      0x02 0x00 (2 bytes)
Val:       <返回请求的 checksum>
Data:      0x00 0x00 (status=成功)
```

---

### 3.17 ERASE_REGION (0xD1) - 擦除 Flash 区域（stub）

**请求：**
```
Direction: 0x00
Command:   0xD1
Size:      0x08 0x00 (8 bytes)
Checksum:  <XOR of data>
Val:       offset[3:0] (Flash 偏移)
Data:      [erase_len:4]
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

## 4. 典型烧录流程

### 4.1 连接与同步

```
烧录器 → 设备: SYNC (0x08)
设备 → 烧录器: SYNC Response [val=checksum, data=00 00 00 00]
```

### 4.2 读取芯片信息

```
烧录器 → 设备: READ_REG (0x0A) [val=0x3FF5A000]
设备 → 烧录器: READ_REG Response [val=checksum, data=chip_id]
```

### 4.3 修改波特率

```
烧录器 → 设备: CHANGE_BAUDRATE (0x0F) [data=new_baud,old_baud]
设备 → 烧录器: CHANGE_BAUDRATE Response [val=checksum]
  (双方切换到新波特率)
```

### 4.4 Flash 烧录（压缩）

```
烧录器 → 设备: FLASH_DEFL_BEGIN (0x10) [size, offset]
设备 → 烧录器: FLASH_DEFL_BEGIN Response

烧录器 → 设备: FLASH_DEFL_DATA (0x11) [seq=0, data]
设备 → 烧录器: FLASH_DEFL_DATA Response

... (重复直到所有数据发送完毕)

烧录器 → 设备: FLASH_DEFL_END (0x12) [reboot=1]
设备 → 烧录器: FLASH_DEFL_END Response
```

---

## 5. 状态码

| 状态码 | 含义 |
|--------|------|
| `0x0000` | 成功 |
| `非0` | 失败 |

---

## 6. 参考资料

- [xingrz-esptool](D:\larryli\xingrz-esptool) - TypeScript 实现
- [esptool 源码](https://github.com/espressif/esptool)
