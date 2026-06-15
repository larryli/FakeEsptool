# FakeEsptool - 烧录流程指南

本文档基于 `esptool write-flash` 的真实交互日志，从使用和交互的角度解读烧录全过程。作为 [PROTOCOL.md](PROTOCOL.md) 的补充，侧重于**流程串联**和**日志解读**，而非单条命令的包格式细节。

**适用场景：** 使用 FakeEsptool 模拟 ESP32-C2 设备，执行以下命令：

```bash
esptool --baud 921600 write-flash 0x0 bootloader.bin 0x10000 app.bin 0x8000 partition-table.bin
```

---

## 1. 全局概览

一次完整的 `write-flash` 烧录会经历 **7 个阶段**：

```
┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐
│ 1. 复位  │──▶│ 2. 同步  │──▶│ 3. 识别  │──▶│ 4. 准备  │──▶│ 5. 烧录  │──▶│ 6. 校验  │──▶│ 7. 重启  │
│ DTR/RTS  │   │  SYNC    │   │ 芯片检测 │   │ Stub上传 │   │ Flash写入│   │  MD5     │   │ 复位启动 │
│          │   │          │   │ MAC读取  │   │ 波特率   │   │ 压缩传输 │   │          │   │          │
└──────────┘   └──────────┘   └──────────┘   └──────────┘   └──────────┘   └──────────┘   └──────────┘
```

**关键数字（本次日志）：**

| 指标 | 值 |
|------|-----|
| 目标芯片 | ESP32-C2（chip_id=12） |
| Flash 大小 | 2MB |
| 烧录段数 | 3（bootloader + app + partition） |
| 总传输数据 | ~72KB 压缩 → ~130KB 原始 |
| 波特率 | 115200 → 921600 |
| 耗时 | ~1.7 秒（含信号复位） |

---

## 2. 阶段详解

### 阶段 1：复位进入下载模式

**用户视角：** esptool 通过 DTR/RTS 信号线控制芯片的 EN 和 GPIO0 引脚，使芯片进入 ROM Bootloader 的下载模式。

**日志表现：**

```
[SIG] DSR:OFF CTS:ON RI:OFF DCD:OFF     ← 第一步：DTR=OFF, RTS=ON
[SIG] DSR:OFF CTS:ON
[SIG] DSR:ON  CTS:OFF RI:OFF DCD:OFF    ← 第二步：DTR=ON,  RTS=OFF（IO0 拉低）
[SIG] DSR:ON  CTS:OFF
[SIG] DSR:OFF CTS:OFF RI:OFF DCD:OFF    ← 第三步：DTR=OFF, RTS=OFF（释放复位）
[SIG] DSR:OFF CTS:OFF
[SIG] Download mode entered              ← 进入下载模式
[ESP]   Protocol state reset             ← 协议状态重置为 IDLE
```

**信号含义：**

| 信号 | 对应引脚 | 作用 |
|------|---------|------|
| DTR | GPIO0 | 高电平=GPIO0 拉低（选择下载模式） |
| RTS | EN (复位) | 高电平=EN 拉低（触发复位） |

**ClassicReset 时序：**
1. `RTS=ON, DTR=OFF` → EN=LOW, GPIO0=HIGH（复位开始，GPIO0 未选择）
2. `DTR=ON, RTS=OFF` → GPIO0=LOW, EN=HIGH（GPIO0 选择下载模式，释放复位）
3. `DTR=OFF, RTS=OFF` → GPIO0=HIGH, EN=HIGH（释放所有信号，芯片启动进入下载模式）

**FakeEsptool 实现：** `main.c` 中的 `OnEsptoolSignal` 函数通过 DSR/CTS 状态变化检测上述时序，识别出下载模式后调用 `OutputBootMessage` 输出启动日志。

---

### 阶段 2：启动日志与 SYNC 同步

**用户视角：** 芯片 ROM Bootloader 启动后，在默认波特率下输出启动日志，然后等待 esptool 的同步握手。

**日志表现：**

```
[CFG] 74880,8N1                              ← 切换到 ESP32-C2 启动波特率
[CFG] Baud rate: 74880
[TX]  45 53 50 2D 52 4F 4D 3A ...            ← ROM 启动日志（原始 ASCII）
[BOOT] ESP-ROM:esp8684-api2-20220127
[BOOT] Build:Jan 27 2022
[BOOT] rst:0x01 (POWERON),boot:0x4 (DOWNLOAD(UART0))
[BOOT] waiting for download

[CFG] 115200,8N1                             ← 切回标准波特率
[CFG] Baud rate: 115200

[RX]  C0 00 08 24 00 ...                     ← SYNC 请求 #1
[ESP] [REQ] SYNC size=36 val=0x00000000
[ESP]   Sync handshake
[TX]  C0 01 08 04 00 07 07 12 55 ...         ← SYNC 响应 ×8
[TX]  C0 01 08 04 00 07 07 12 55 ...
...（共 8 次）

[RX]  C0 00 08 24 00 ...                     ← SYNC 请求 #2（重试）
[ESP] [REQ] SYNC size=36 val=0x00000000
[TX]  C0 01 08 04 00 07 07 12 55 ...         ← SYNC 响应 ×8
...（共 8 次）
```

**要点解读：**

1. **74880 波特率**：ESP32-C2（26MHz XTAL）的 ROM Bootloader 使用 74880 波特率输出启动日志。其他芯片（如 ESP32、ESP32-S3）使用 115200。FakeEsptool 在 `Chip_GetBootBaudRate` 中根据芯片类型和 XTAL 频率决定。

2. **启动日志格式**：每款芯片的 ROM 启动日志格式不同，包含 ROM 版本标识、复位原因、启动模式。详见 [REQUIREMENTS.md](REQUIREMENTS.md) 的"启动日志"章节。

3. **SYNC 重试**：esptool 发送 2 次 SYNC 请求（可配置更多），每次设备返回 8 个相同响应。这是为了应对串口丢包——只要客户端收到任意一个响应即可。

4. **SYNC 响应 Val 字段**：`0x07071255`（小端序），其中 `07 07 12` 是固定的同步签名，`55` 来自请求 Data 的第一个填充字节。

**协议细节：** SYNC 请求的 Data 为 36 字节，格式为 `[07 07 12 20] + [55 × 32]`。Checksum 固定为 `0x00000000`（不计算）。详见 [PROTOCOL.md](PROTOCOL.md) §3.2。

---

### 阶段 3：芯片识别与信息读取

**用户视角：** esptool 需要知道连接的是哪款芯片，才能决定后续的 Stub 代码、Flash 参数等。

**日志表现：**

```
[RX]  C0 00 14 00 00 ...                       ← GET_SECURITY_INFO #1
[ESP] [REQ] GET_SECURITY_INFO size=0 val=0x00000000
[ESP]   Get security info
[ESP]   flags=0x00000000 flash_crypt_cnt=0
[ESP]   chip_id=12 (0x0000000C) api_version=0   ← chip_id=12 → ESP32-C2
[ESP]   Chip detected via security info, ready for commands
[TX]  C0 01 14 16 00 00 00 00 00 ...            ← 22 字节响应

[RX]  C0 00 14 00 00 ...                       ← GET_SECURITY_INFO #2（确认）
[ESP] [REQ] GET_SECURITY_INFO size=0 val=0x00000000
[ESP]   chip_id=12 (0x0000000C) api_version=0

[RX]  C0 00 0A 04 00 44 88 00 60                ← READ_REG 0x60008844（MAC word1）
[ESP] [REQ] READ_REG size=4 val=0x00000000
[ESP]   addr=0x60008844 -> 0x0010AABB
[TX]  C0 01 0A 04 00 BB AA 10 00 ...

[RX]  C0 00 0A 04 00 44 88 00 60                ← READ_REG 0x60008844（重复）
...（读取 4 次）

[RX]  C0 00 0A 04 00 5C 88 00 60                ← READ_REG 0x6000885C
[ESP]   addr=0x6000885C -> 0x00000000

[RX]  C0 00 0A 04 00 14 00 00 60                ← READ_REG 0x60000014（UART 时钟分频器）
[ESP]   addr=0x60000014 -> 0x000000E1
```

**要点解读：**

1. **芯片检测优先级**：esptool 优先使用 `GET_SECURITY_INFO`（命令 0x14）获取 `chip_id`。如果芯片不支持（如 ESP8266/ESP32），则回退到 `READ_REG 0x40001000` 读取 magic value。

2. **chip_id 映射**：`chip_id=12` 对应 ESP32-C2。各芯片的 IMAGE_CHIP_ID 见 [PROTOCOL.md](PROTOCOL.md) 附录。

3. **MAC 读取**：通过 `READ_REG` 读取 eFuse 寄存器获取 MAC 地址。ESP32-C2 的 MAC 存储在 eFuse 偏移 0x40 处（word16=0x60008840, word17=0x60008844）。

4. **重复读取**：日志中 `0x60008844` 被读取 4 次——这是 esptool 的标准行为，用于确认读取一致性。

5. **UART 时钟分频器**：`0x60000014` 返回 `0xE1`（225），esptool 用此计算实际晶振频率：`freq = baudrate × divider / 1000000`。

**协议细节：** GET_SECURITY_INFO 响应格式为 `[flags:4][flash_crypt_cnt:1][key_purposes:7][chip_id:4][api_version:4][status:2]`（22 字节）。详见 [PROTOCOL.md](PROTOCOL.md) §3.22。

---

### 阶段 4：Stub 上传与波特率切换

**用户视角：** esptool 将一段精简的 "Stub" 程序上传到芯片 RAM 中运行，替代 ROM Bootloader 实现更高效的烧录操作。

**日志表现：**

```
[RX]  C0 00 05 10 00 ...                        ← MEM_BEGIN（text 段）
[ESP] [REQ] MEM_BEGIN size=16 val=0x00000000
[ESP]   total=6024 blocks=1 bsize=6144 offset=0x40380000

[RX]  C0 00 07 98 17 ...                        ← MEM_DATA（6040 字节 Stub 代码）
[ESP] [REQ] MEM_DATA size=6040 val=0x0000005E
[ESP]   seq=6024 len=6040

[RX]  C0 00 05 10 00 ...                        ← MEM_BEGIN（data 段）
[ESP] [REQ] MEM_BEGIN size=16 val=0x00000000
[ESP]   total=204 blocks=1 bsize=6144 offset=0x3FCB6D6C

[RX]  C0 00 07 DC 00 ...                        ← MEM_DATA（220 字节 Stub 数据）
[ESP] [REQ] MEM_DATA size=220 val=0x00000070
[ESP]   seq=204 len=220

[RX]  C0 00 06 08 00 ...                        ← MEM_END（启动 Stub）
[ESP] [REQ] MEM_END size=8 val=0x00000000
[ESP]   execute=0

[TX]  C0 4F 48 41 49 C0                         ← OHAI 握手（Stub 就绪）
[ESP]   Stub mode: OHAI sent
```

**要点解读：**

1. **Stub 是什么**：Stub 是一段预编译的 RISC-V/ARM 代码，上传到芯片 RAM 后接管通信。相比 ROM Bootloader，Stub 支持：
   - 压缩传输（DEFLATE）
   - 更大的数据块
   - 更多命令（如 `ERASE_FLASH`、`READ_FLASH`）
   - 更高的波特率

2. **两段上传**：Stub 分为 text 段（代码，`0x40380000`）和 data 段（数据，`0x3FCB6D6C`），分别通过 `MEM_BEGIN` + `MEM_DATA` + `MEM_END` 上传。

3. **OHAI 握手**：`MEM_END` 后，Stub 在串口上发送 `C0 4F 48 41 49 C0`（SLIP 帧包裹的 ASCII "OHAI"），表示已就绪。这是 Stub 模式特有的握手信号。

4. **execute=0**：本次日志中 `MEM_END` 的 execute 字段为 0，但 Stub 仍然启动——这是因为 `MEM_END` 后设备会自动跳转到 text 段的入口地址。

**Stub 上传后的模式切换：**

| 特性 | ROM 模式（上传前） | Stub 模式（上传后） |
|------|-------------------|-------------------|
| Status 长度 | 4 字节 | 2 字节 |
| 压缩写入 | 支持 | 支持 |
| MD5 响应 | 32 字节 ASCII | 16 字节二进制 |
| 最大块大小 | 通常 0x400 | 通常 0x4000 |

**日志中的模式切换标志：** 从 `MEM_END` 之后，所有响应的 Status 长度从 4 字节变为 2 字节。

---

### 阶段 5：Flash 烧录（压缩模式）

**用户视角：** esptool 使用 DEFLATE 压缩传输各段数据，设备端解压后写入 Flash。这是烧录的核心阶段。

**日志表现（以 bootloader 段为例）：**

```
[RX]  C0 00 10 14 00 ...                         ← FLASH_DEFL_BEGIN
[ESP] [REQ] FLASH_DEFL_BEGIN size=20 val=0x00000000
[ESP]   uncompressed=19696 blocks=1 bsize=16384 offset=0x00000000
[ESP]   Flash erased: offset=0x00000000 size=19696

[RX]  C0 00 11 53 30 ...                         ← FLASH_DEFL_DATA（12355 字节压缩数据）
[ESP] [REQ] FLASH_DEFL_DATA size=12371 val=0x00000048
[ESP]   seq=0 len=12355
[ESP]   Accumulated 12355/19696 bytes

[RX]  C0 00 12 04 00 ...                         ← FLASH_DEFL_END
[ESP] [REQ] FLASH_DEFL_END size=4 val=0x00000000
[ESP]   End compressed flash download
[ESP]   Decompressed 12355 -> 19696 bytes at offset 0x00000000
```

**三段烧录摘要：**

| 段 | 偏移 | 压缩大小 | 原始大小 | 压缩率 | DATA 包数 |
|----|------|---------|---------|--------|----------|
| bootloader | 0x00000000 | 12,355 B | 19,696 B | 63% | 1 |
| app | 0x00010000 | 59,832 B | 107,744 B | 56% | 4 |
| partition-table | 0x00008000 | 103 B | 3,072 B | 3% | 1 |

**要点解读：**

1. **压缩传输流程**：
   ```
   FLASH_DEFL_BEGIN → [擦除 Flash 区域] → 分配解压缓冲区
   FLASH_DEFL_DATA × N → 累积压缩数据到缓冲区
   FLASH_DEFL_END → 解压全部数据 → 写入 Flash → 释放缓冲区
   ```

2. **Flash 擦除**：`FLASH_DEFL_BEGIN` 时设备根据 `uncompressed_size` 擦除目标区域（扇区对齐，4KB 边界）。擦除在数据传输前完成。

3. **Checksum 校验**：每个 `FLASH_DEFL_DATA` 包的 Val 字段包含 payload 的 XOR 校验和（初始值 0xEF）。设备端验证不通过则返回失败。

4. **序列号校验**：每个 `FLASH_DEFL_DATA` 包的 Data 前 4 字节包含序列号（seq），从 0 递增。设备端验证连续性。

5. **多段烧录**：每段独立执行 `FLASH_DEFL_BEGIN` → `FLASH_DEFL_DATA` × N → `FLASH_DEFL_END`。段之间不需要额外的命令。

6. **数据包格式**：
   ```
   FLASH_DEFL_DATA 请求 Data: [data_len:4][seq:4][padding:4][padding:4][compressed_data:data_len]
   ```

**实现细节：** FakeEsptool 使用"积累解压"方案——将所有压缩数据累积到缓冲区，在 `FLASH_DEFL_END` 时一次性解压写入。详见 [DEVELOPMENT.md](DEVELOPMENT.md) 的"积累解压方案"章节。

---

### 阶段 6：MD5 校验

**用户视角：** 每段烧录完成后，esptool 计算设备端 Flash 的 MD5 与本地文件比对，确保数据完整性。

**日志表现：**

```
[RX]  C0 00 13 10 00 ...                         ← SPI_FLASH_MD5 请求
[ESP] [REQ] SPI_FLASH_MD5 size=16 val=0x00000000
[ESP]   addr=0x00000000 len=19696
[ESP]   MD5 (stub, binary)
[TX]  C0 01 13 12 00 00 00 00 00                 ← 16 字节二进制 MD5 + 2 字节 status
      8D 35 AD 37 25 69 C3 D6
      66 AC F9 D5 7F A6 5F C5
      00 00
```

**要点解读：**

1. **Stub 模式返回二进制 MD5**：16 字节原始 MD5 哈希（非 ASCII）。ROM 模式返回 32 字节 ASCII 十六进制字符串。

2. **MD5 位于 Data 字段前部**，Status（2 字节 `0x00 0x00`）位于末尾。

3. **esptool 校验失败时**：输出 `A fatal error occurred: MD5 of file does not match data in flash!` 并中止。

**本次日志三段 MD5 校验结果：**

| 段 | 地址 | 长度 | 结果 |
|----|------|------|------|
| bootloader | 0x00000000 | 19,696 | PASS（MD5 匹配） |
| app | 0x00010000 | 107,744 | PASS（MD5 匹配） |
| partition-table | 0x00008000 | 3,072 | PASS（MD5 匹配） |

---

### 阶段 7：硬复位重启

**用户视角：** 烧录完成，esptool 通过 DTR/RTS 信号触发硬复位，芯片从 Flash 正常启动。

**日志表现：**

```
[SIG] DSR:OFF CTS:ON RI:OFF DCD:OFF             ← RTS=ON（EN 拉低，复位开始）
[SIG] DSR:OFF CTS:ON
[SIG] DSR:OFF CTS:OFF RI:OFF DCD:OFF            ← RTS=OFF（EN 释放，复位结束）
[SIG] DSR:OFF CTS:OFF
[SIG] Hard reset (normal boot)                   ← 识别为硬复位（非下载模式）
[ESP]   Protocol state reset

[CFG] 74880,8N1                                  ← 切换到启动波特率
[CFG] Baud rate: 74880
[TX]  ESP-ROM:esp8684-api2-20220127              ← 正常启动日志
[BOOT] rst:0x02 (EXT),boot:0x8 (SPI_FAST_FLASH_BOOT)
[BOOT] SPIWP:0xee
[BOOT] mode:DIO, clock div:1
[BOOT] load:0x3fff0008,len:8
[BOOT] load:0x3fff0010,len:3680
[BOOT] load:0x40078000,len:8364
[BOOT] load:0x40080000,len:252
[BOOT] entry 0x40080034
```

**要点解读：**

1. **下载模式 vs 正常启动**：FakeEsptool 通过 DTR/RTS 信号时序区分：
   - 经典复位序列（先 RTS 后 DTR）→ 下载模式
   - 仅 RTS 脉冲（无 DTR）→ 正常启动

2. **正常启动日志**：包含 SPI Flash 配置、固件段加载信息、入口地址。格式因芯片而异。

3. **复位原因**：`rst:0x02 (EXT)` 表示外部复位（由 DTR/RTS 信号触发），`boot:0x8 (SPI_FAST_FLASH_BOOT)` 表示从 SPI Flash 启动。

---

## 3. 日志行格式解读

FakeEsptool 日志每行格式：

```
时间戳 +Δ偏移 [标签] 内容
```

| 组成部分 | 示例 | 说明 |
|---------|------|------|
| 时间戳 | `2026-06-15 10:04:03.721` | 绝对时间（毫秒精度） |
| Δ偏移 | `+19.542` | 距上一条日志的时间差（秒） |
| 标签 | `[SIG]` `[ESP]` `[RX]` `[TX]` `[CFG]` `[BOOT]` `[ERR]` | 消息类别 |
| 内容 | `DSR:OFF CTS:ON` | 具体信息 |

**标签含义：**

| 标签 | 来源 | 含义 |
|------|------|------|
| `[SIG]` | 信号检测 | DTR/RTS/DSR/CTS 信号变化 |
| `[ESP]` | 协议层 | esptool 命令解析与响应 |
| `[RX]` | 串口层 | 从串口接收到的原始数据（hex dump） |
| `[TX]` | 串口层 | 发送到串口的原始数据（hex dump） |
| `[CFG]` | 配置层 | 波特率/数据位等配置变化 |
| `[BOOT]` | 启动 | ROM Bootloader 启动日志 |
| `[ERR]` | 错误 | 错误信息 |

**[RX]/[TX] hex dump 格式：**

```
[RX] C0 00 08 24 00 00 00 00  00 07 07 12 20 55 55 55  |...$........ UUU|
     55 55 55 55 55 55 55 55  55 55 55 55 55 55 55 55  |UUUUUUUUUUUUUUUU|
     55 55 55 55 55 55 55 55  55 55 55 55 55 C0        |UUUUUUUUUUUUU.|
```

- 左侧：十六进制字节（每 8 字节一组，组间空格）
- 右侧：ASCII 解码（不可打印字符显示为 `.`）
- `C0` 是 SLIP 帧定界符
- 第一个 `C0` 后的字节是 Direction（`00`=请求, `01`=响应）

**注意**：ASCII 解码部分可通过编译选项 `LOG_NOT_SHOW_ASCII=ON` 禁用（参见 [DEVELOPMENT.md](DEVELOPMENT.md)），禁用后日志仅显示十六进制数据，适合 Agents 分析。

禁用 ASCII 后的日志格式：
```
[RX] C0 00 08 24 00 00 00 00 00 07 07 12 20 55 55 55
     55 55 55 55 55 55 55 55 55 55 55 55 55 55 55 55
     55 55 55 55 55 55 55 55 55 55 55 55 55 C0
```

**[ESP] 命令日志格式：**

```
[ESP] [REQ] FLASH_DEFL_DATA size=16400 val=0x0000008B
[ESP]   seq=0 len=16384
[ESP]   Accumulated 16384/107744 bytes
```

- `[REQ]`/`[RES]`：请求或响应
- `size=`：Data 字段大小
- `val=`：Value 字段值
- 缩进行：命令特定的详细信息

---

## 4. ROM 模式 vs Stub 模式

日志中可以清晰看到两种模式的分界点——`OHAI` 握手之后即为 Stub 模式。

| 特征 | ROM 模式（OHAI 之前） | Stub 模式（OHAI 之后） |
|------|---------------------|----------------------|
| 命令 Status 长度 | 4 字节 | 2 字节 |
| SYNC 响应数 | 每次请求 8 个响应 | 同 |
| GET_SECURITY_INFO | ✓ | ✓ |
| SPI_SET_PARAMS | ESP8266 不支持 | ✓ |
| ERASE_FLASH (0xD0) | ✗ 返回 ROM_INVALID_RECV_MSG | ✓ |
| READ_FLASH (0xD2) | ✗ | ✓ |
| MD5 响应格式 | 32 字节 ASCII hex | 16 字节二进制 |
| 压缩写入 | ✓（直接解压写入） | ✓（积累后解压写入） |

**日志中的模式识别方法：**

1. **看 OHAI**：`[TX] C0 4F 48 41 49 C0` 出现后即为 Stub 模式
2. **看 Status 长度**：响应 Data 字段末尾，4 字节=ROM，2 字节=Stub
3. **看 MD5 响应**：32 字节=ROM，16 字节=Stub

---

## 5. 典型问题排查

### 问题 1：SYNC 失败

**日志特征：**
```
[RX] C0 00 08 24 00 ...    ← 发送了 SYNC
（无响应）
```

**可能原因：**
- 芯片未进入下载模式（DTR/RTS 信号时序错误）
- 串口连接问题（TX/RX 接反）
- 波特率不匹配

**排查方法：** 检查 `[SIG]` 日志是否出现 `Download mode entered`。

### 问题 2：芯片识别失败

**日志特征：**
```
[ESP] [REQ] GET_SECURITY_INFO → 返回 chip_id=0 或错误
[ESP] [REQ] READ_REG 0x40001000 → 返回 0x00000000
```

**可能原因：**
- 芯片不支持 GET_SECURITY_INFO（回退到 magic value 检测）
- 芯片未正确初始化
- eFuse 数据异常

### 问题 3：Flash 烧录失败

**日志特征：**
```
[ESP] [REQ] FLASH_DEFL_DATA ... checksum mismatch
```

**可能原因：**
- 串口数据传输错误（噪声、波特率不匹配）
- 芯片端缓冲区溢出

### 问题 4：MD5 不匹配

**日志特征：**
```
[ESP] [REQ] SPI_FLASH_MD5 → 返回的 MD5 与本地不一致
```

**可能原因：**
- Flash 写入不完整（被中断）
- Flash 擦除不充分
- 压缩数据解压错误

### 问题 5：Stub 上传失败

**日志特征：**
```
[ESP] [REQ] MEM_DATA ... checksum mismatch
[ESP] [RES] MEM_DATA size=4 status=0x00000001    ← status=1 表示失败
```

**可能原因：**
- MEM_DATA 校验和错误
- 内存地址冲突
- 序列号不连续

---

## 6. 与 PROTOCOL.md 的交叉索引

本指南涉及的命令在 PROTOCOL.md 中的详细定义：

| 本指南阶段 | 命令 | PROTOCOL.md 章节 |
|-----------|------|-----------------|
| 阶段 2 | SYNC (0x08) | §3.2 |
| 阶段 3 | GET_SECURITY_INFO (0x14) | §3.22 |
| 阶段 3 | READ_REG (0x0A) | §3.3 |
| 阶段 4 | MEM_BEGIN (0x05) | §3.8 |
| 阶段 4 | MEM_DATA (0x07) | §3.9 |
| 阶段 4 | MEM_END (0x06) | §3.10 |
| 阶段 4 | CHANGE_BAUDRATE (0x0F) | §3.7 |
| 阶段 5 | FLASH_DEFL_BEGIN (0x10) | §3.14 |
| 阶段 5 | FLASH_DEFL_DATA (0x11) | §3.15 |
| 阶段 5 | FLASH_DEFL_END (0x12) | §3.16 |
| 阶段 6 | SPI_FLASH_MD5 (0x13) | §3.18 |
| 阶段 5 | SPI_SET_PARAMS (0x0B) | §3.5 |
| 阶段 5 | WRITE_REG (0x09) | §3.4 |

包格式、Checksum 计算规则、Status 码定义等详见 [PROTOCOL.md](PROTOCOL.md) §2。

---

## 7. 附录：完整日志时序表

以下为本次日志的完整时序摘要（省略重复的 SYNC 响应和 WRITE_REG 细节）：

| 时间 | Δ | 方向 | 命令/事件 | 关键参数 |
|------|---|------|----------|---------|
| 10:04:03.721 | - | SIG | DSR:OFF CTS:ON | 复位开始 |
| 10:04:03.825 | +104ms | SIG | DSR:ON CTS:OFF | IO0 拉低 |
| 10:04:03.876 | +51ms | SIG | DSR:OFF CTS:OFF | 复位结束，进入下载模式 |
| 10:04:03.910 | +34ms | CFG | 波特率 74880 | ESP32-C2 启动波特率 |
| 10:04:03.910 | +0ms | TX | ROM 启动日志 | `waiting for download` |
| 10:04:03.958 | +48ms | CFG | 波特率 115200 | 切回标准波特率 |
| 10:04:03.958 | +0ms | RX | SYNC #1 | 36 字节请求 |
| 10:04:03.958 | +0ms | TX | SYNC 响应 ×8 | Val=0x07071255 |
| 10:04:03.962 | +4ms | RX | SYNC #2 | 重试 |
| 10:04:03.962 | +0ms | TX | SYNC 响应 ×8 | |
| 10:04:03.962 | +0ms | RX | GET_SECURITY_INFO | chip_id=12 (ESP32-C2) |
| 10:04:03.962 | +0ms | RX | READ_REG ×多次 | MAC/eFuse/UART 读取 |
| 10:04:03.962 | +0ms | RX | MEM_BEGIN | text 段, 6024B, @0x40380000 |
| 10:04:04.181 | +219ms | RX | MEM_DATA | 6040 字节 Stub 代码 |
| 10:04:04.181 | +0ms | RX | MEM_BEGIN | data 段, 204B, @0x3FCB6D6C |
| 10:04:04.181 | +0ms | RX | MEM_DATA | 220 字节 Stub 数据 |
| 10:04:04.181 | +0ms | RX | MEM_END | execute=0 |
| 10:04:04.181 | +0ms | TX | **OHAI** | Stub 就绪 |
| 10:04:04.181 | +0ms | RX | CHANGE_BAUDRATE | 115200 → 921600 |
| 10:04:04.193 | +12ms | RX | WRITE_REG 序列 | SPI 控制器初始化 |
| 10:04:04.341 | +148ms | RX | READ_REG | 读取 Flash ID (0x001540EF) |
| 10:04:04.955 | +614ms | RX | SPI_SET_PARAMS | Flash 参数配置 |
| 10:04:04.955 | +0ms | RX | FLASH_DEFL_BEGIN | **bootloader**, @0x00000000, 19696B |
| 10:04:05.157 | +200ms | RX | FLASH_DEFL_DATA ×1 | 12355B 压缩 |
| 10:04:05.157 | +0ms | RX | FLASH_DEFL_END | 解压 → 19696B |
| 10:04:05.157 | +0ms | RX | SPI_FLASH_MD5 | bootloader 校验 PASS |
| 10:04:05.300 | +143ms | RX | FLASH_DEFL_BEGIN | **app**, @0x00010000, 107744B |
| 10:04:05.300~.202 | - | RX | FLASH_DEFL_DATA ×4 | 59832B 压缩 |
| 10:04:05.202 | +0ms | RX | FLASH_DEFL_END | 解压 → 107744B |
| 10:04:05.202 | +0ms | RX | SPI_FLASH_MD5 | app 校验 PASS |
| 10:04:05.300 | +98ms | RX | FLASH_DEFL_BEGIN | **partition**, @0x00008000, 3072B |
| 10:04:05.423 | +123ms | RX | FLASH_DEFL_DATA ×1 | 103B 压缩 |
| 10:04:05.439 | +16ms | RX | FLASH_DEFL_END | 解压 → 3072B |
| 10:04:05.439 | +0ms | RX | SPI_FLASH_MD5 | partition 校验 PASS |
| 10:04:05.439 | +0ms | SIG | DSR:OFF CTS:ON | 硬复位开始 |
| 10:04:05.535 | +96ms | SIG | DSR:OFF CTS:OFF | 复位结束，正常启动 |
| 10:04:05.550 | +15ms | CFG | 波特率 74880 | 启动波特率 |
| 10:04:05.552 | +2ms | TX | 正常启动日志 | `SPI_FAST_FLASH_BOOT` |
