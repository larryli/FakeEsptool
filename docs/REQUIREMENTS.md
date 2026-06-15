# FakeEsptool - 需求规格说明

## 项目概述

ESP 芯片设备端模拟器，支持虚拟串口。程序模拟 ESP8266/ESP32 系列芯片，响应 esptool 客户端的烧录协议。

- **技术栈**: C + CMake + Win32 API
- **波特率**: 115200, 8N1（默认，支持动态修改）
- **串口选择**: 弹出对话框手动选择
- **协议**: esptool SLIP 协议

---

## 菜单结构

### File 文件菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| New Device | 创建默认设备（ESP32/40MHz/4MB） | 始终可用 |
| Open Device... | 加载 .esp 文件 | 见状态管理 |
| Save Device | 保存到当前文件 | 始终可用 |
| Save Device As... | 另存为 | 始终可用 |
| Dump Device As... | 导出设备内容到 txt 文件 | 始终可用 |
| Device Properties... | 修改设备参数 | 见状态管理 |
| Exit | 退出程序 | 见状态管理 |

**Save Device 详细要求：**
- 已有文件名时：保存到当前文件
- 无文件名时：执行 Save Device As
- 保存成功后更新 LastFile 配置

**Save Device As... 详细要求：**
- 弹出标准文件保存对话框
- 默认文件名格式: `[Chip]-[Flash Size]`（如 `ESP32-4MB`）
- 文件类型过滤: `*.esp` / `*.*`
- 覆盖已有文件时弹出确认提示
- 保存成功后更新 LastFile 配置

**Dump Device As... 详细要求：**
- 弹出标准文件保存对话框
- 默认文件名格式: `<设备名>_dump.txt`
- 文件类型过滤: `*.txt` / `*.*`
- 覆盖已有文件时弹出确认提示
- 保存为 UTF-8 编码（无 BOM）
- 输出内容：
  - 设备头信息（Magic、Version、Chip Type、XTAL Freq）
  - MAC 地址
  - Flash 配置（大小）
  - eFuse 数据（完整，hex dump 格式）
  - Flash 数据（完整，hex dump 格式）
- 实现方式：
  - 先生成设备数据快照（eFuse + Flash）
  - 使用后台线程执行文件写入
  - 写入期间显示忙碌光标，禁用主窗口
  - 完成后通过消息通知主线程恢复

### Serial 串口菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| Connect | 弹出串口选择对话框 | 已连接时禁用（灰色） |
| Reconnect | 直接连接上次端口 | 未连接且上次端口可用时启用，已连接时禁用 |
| Disconnect | 断开当前串口 | 未连接时禁用（灰色） |

### Flash 菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| Import Flash... | 从 .bin 文件导入 Flash 数据 | 见状态管理 |
| Export Flash... | 导出 Flash 数据到 .bin 文件 | 始终可用 |
| Key Management... | 打开密钥管理对话框 | ESP8266 禁用（不支持 Flash 加密），连接时提示断开 |

**Import Flash 详细要求：**
- 见状态管理（连接时提示断开，修改时提示保存）
- 检查文件大小与当前 Flash 大小是否一致
- 不一致时显示错误提示
- 导入期间显示忙碌光标，禁用主窗口

**Export Flash 详细要求：**
- 弹出标准文件保存对话框
- 覆盖已有文件时弹出确认提示
- 导出前生成 Flash 数据快照
- 导出期间显示忙碌光标，禁用主窗口

### Log 日志菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| Clear | 清空主窗口所有日志 | 始终可用 |
| Save as... | 保存日志到文件 | 始终可用 |
| Font... | 字体设置对话框 | 始终可用 |

**Save as... 详细要求：**
- 弹出标准文件保存对话框
- 默认文件名格式: `FakeEsptool_YYYYMMDD_HHMMSS.log`
- 文件类型过滤: `*.log` / `*.*`
- 覆盖已有文件时弹出确认提示
- 保存为 UTF-8 编码（无 BOM）

**Font... 详细要求：**
- 弹出标准字体选择对话框
- 只显示等宽（固定宽度）字体
- 可选择字体大小
- 设置立即生效并保存到配置文件

### Help 帮助菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| About | 显示关于对话框 | 始终可用 |

---

## 设备管理

### 默认设备

程序启动或新建设备时自动创建默认设备：

| 参数 | 默认值 |
|------|--------|
| 芯片类型 | ESP32 |
| 晶振频率 | 40MHz |
| Flash 大小 | 4MB |
| MAC 地址 | AA:BB:CC:DD:EE:01 |

### Device Properties 对话框

| 字段 | 类型 | 说明 |
|------|------|------|
| 芯片类型 | 下拉选择 | ESP8266/ESP32/S2/S3/C2/C3/C6 |
| 晶振频率 | 下拉选择 | 40MHz/26MHz（ESP32-C3/C6/S2/S3 固定 40MHz 禁用） |
| Flash 大小 | 下拉选择 | 256KB/512KB/1MB/2MB/4MB/8MB/16MB |
| MAC 地址 | 输入框 | 支持手动输入和随机生成 |

**说明**：修改参数后设备数据标记为已修改，需保存。切换芯片类型时保留原有 Flash 数据（大小不变时）。

**按钮行为**：
- 取消：无副作用，保留原设备状态（包括 modified 标记）
- 确定+参数无变化：无副作用
- 确定+参数有变化：等价于新建指定参数设备，标记修改（modified=TRUE）

### Key Management 对话框

管理加密密钥的导入、导出和生成。

**对话框布局**：
- 标题栏：显示当前芯片类型（如 "Key Management - ESP32-S3"）
- 列表控件：显示所有可用密钥块
- 按钮：Import、Export、Generate、Close

**列表列**：

| 列名 | 说明 |
|------|------|
| Block | 密钥块名称（BLOCK_KEY0, BLOCK_KEY1, ...） |
| Purpose | 密钥用途（Flash Encryption, Secure Boot V2, unused） |
| Status | 编程状态（Programmed, Empty） |
| Size | 密钥长度（128-bit, 256-bit） |

**动态内容**：根据当前芯片类型显示对应的密钥块

| 芯片 | 密钥块数量 | 密钥长度 |
|------|-----------|---------|
| ESP32 | 1 (BLOCK_KEY0) | 256-bit |
| ESP32-S2/S3 | 6 (KEY0-KEY5) | 256-bit |
| ESP32-C2 | 1 (BLOCK_KEY0) | 128-bit |
| ESP32-C3/C6 | 6 (KEY0-KEY5) | 256-bit |

**按钮行为**：

| 按钮 | 功能 | 说明 |
|------|------|------|
| Import | 从文件导入密钥 | 弹出文件选择对话框，选择 .bin 文件 |
| Export | 导出密钥到文件 | 弹出文件保存对话框，保存为 .bin 文件 |
| Generate | 生成随机密钥 | 生成随机密钥并写入选中的密钥块 |
| Close | 关闭对话框 | 如有修改，标记设备为已修改 |

**状态逻辑**：
- 串口连接时禁用（灰色）
- 密钥已编程时，Export 按钮启用
- 密钥为空时，Export 按钮禁用

### 设备文件格式

- 文件扩展名: `.esp`
- 二进制格式，版本 1

**文件结构：**

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0x00 | 4B | magic | 魔数 `0x45535000` ("ESP\0") |
| 0x04 | 4B | version | 格式版本 (1) |
| 0x08 | 4B | chipType | 芯片类型枚举 |
| 0x0C | 1B | xtalFreq | 晶振频率枚举 (0=40MHz, 1=26MHz) |
| 0x0D | 3B | reserved | 保留（零填充） |
| 0x10 | 6B | mac | MAC 地址 |
| 0x16 | 2B | reserved | 保留（零填充） |
| 0x18 | 4B | flashSize | Flash 大小（字节） |
| 0x1C | 4B | efuseSize | eFuse 数据大小 |
| 0x20 | N | efuse | eFuse 数据 |
| 0x20+N | M | flash | Flash 数据 |

---

## 状态管理

### 操作检查顺序

| 场景 | 检查顺序 |
|------|----------|
| 关闭程序 (Exit/X) | 1. 串口连接 → 2. 数据修改 |
| New Device | 1. 串口连接 → 2. 数据修改 |
| Open Device | 1. 串口连接 → 2. 数据修改 |
| Device Properties | 1. 串口连接 → 2. 数据修改 |
| Import Flash | 1. 串口连接 → 2. 数据修改 |
| Key Management | 1. 串口连接 → 2. 数据修改 |

### 串口连接检查

当串口已连接时执行 New/Open/Exit/Device Properties/Import Flash/Key Management：

```
┌─────────────────────────────────────┐
│  Serial port is connected.          │
│  Do you want to disconnect?         │
│                                     │
│       [Yes]      [No]               │
└─────────────────────────────────────┘
```

- Yes: 断开串口，继续后续检查
- No: 取消操作

### 数据修改检查

当 eFuse 或 Flash 数据被修改后执行 New/Open/Exit/Device Properties/Import Flash：

```
┌─────────────────────────────────────┐
│  Device data has been modified.     │
│  Do you want to save changes?       │
│                                     │
│    [Yes]    [No]    [Cancel]        │
└─────────────────────────────────────┘
```

- Yes: 保存（无文件名则另存为），继续操作
- No: 丢弃修改，继续操作
- Cancel: 取消操作

### 修改标记触发点

| 操作 | 设置 modified |
|------|---------------|
| New Device | 否 |
| Device Properties（修改参数） | 是 |
| eFuse 写入 | 是 |
| Flash 写入 | 是 |
| Flash 擦除 | 是 |
| Flash Import | 是 |
| Key Import/Generate | 是 |
| Save Device | 清除 |
| Load Device | 清除 |

---

## 工具栏

| 按钮 | 资源 | 对应菜单 | 状态逻辑 |
|------|------|----------|----------|
| 新建 | toolbar.bmp[0] | File/New Device | 始终可用 |
| 打开 | toolbar.bmp[1] | File/Open Device | 始终可用 |
| 保存 | toolbar.bmp[2] | File/Save Device | 始终可用 |
| *(分隔符)* | - | - | - |
| 设备属性 | toolbar.bmp[3] | File/Device Properties | 始终可用 |
| *(分隔符)* | - | - | - |
| 连接 | toolbar.bmp[4] | Serial/Connect | 已连接时禁用 |
| 重连 | toolbar.bmp[5] | Serial/Reconnect | 上次端口可用时启用 |
| 断开 | toolbar.bmp[6] | Serial/Disconnect | 未连接时禁用 |
| *(分隔符)* | - | - | - |
| 导入 | toolbar.bmp[7] | Flash/Import | 始终可用 |
| 导出 | toolbar.bmp[8] | Flash/Export | 始终可用 |
| *(分隔符)* | - | - | - |
| 清除 | toolbar.bmp[9] | Log/Clear | 始终可用 |
| 保存日志 | toolbar.bmp[10] | Log/Save as... | 始终可用 |

**工具栏要求：**
- 使用合并的 BMP 位图（11个16x16彩色图标）
- 只显示图标，不显示文本
- 鼠标悬停显示提示文字

---

## 主窗口

### 基本要求
- 多行只读 RichEdit 控件
- 浅灰色背景（RGB 240,240,240）
- 等宽字体（默认 Consolas，可通过配置修改）
- 连接串口时自动清空所有内容

### 彩色显示方案

| 元素 | 颜色 |
|------|------|
| 时间戳 | 灰色 RGB(128,128,128) |
| [RX] 方向标记 | 蓝色 RGB(0,0,200) |
| [TX] 方向标记 | 绿色 RGB(0,128,0) |
| [SIG] 信号标记 | 紫色 RGB(128,0,128) |
| [CFG] 配置标记 | 青色 RGB(0,128,128) |
| [TAG] 自定义标记 | 橙色 RGB(200,100,0) |
| HEX 数据 | 黑色 RGB(0,0,0) |

### 日志格式

```
2026-05-29 14:30:25.123 [RX] C0 00 08 00 00 00 00 00  00 EF C0
2026-05-29 14:30:25.124 [TX] C0 01 08 00 24 00 00 00  07 07 12 20 55 55 55 55
                             55 55 55 55 55 55 55 55  55 55 55 55 55 55 55 55
                             55 55 55 55 55 55 55 55  55 55 55 55 55 55 55 55
                             C0
```

**格式规则：**
1. 时间戳: `YYYY-MM-DD HH:MM:SS.mmm`（精确到毫秒）
2. 方向标记: `[RX]` 或 `[TX]`
3. HEX 数据: 每字节2位大写十六进制，空格分隔
4. 每8字节后额外增加1个空格（视觉分组）
5. 每16字节换行
6. 续行用空格填充对齐到 HEX 起始位置

---

## 状态栏

| 栏位 | 内容 | 宽度 |
|------|------|------|
| 第1栏 | 芯片类型（如 `ESP8266`） | 固定 100px |
| 第2栏 | Flash 大小（如 `4MB`） | 固定 80px |
| 第3栏 | MAC 地址（如 `AA:BB:CC:DD:EE:01`） | 固定 150px |
| 第4栏 | 串口号（如 `COM10`）或 `Disconnected` | 固定 120px |
| 第5栏 | 串口配置（如 `115200,8N1`） | 固定 140px |

---

## 关于对话框

- 应用图标
- 标题: "About FakeEsptool"
- 内容:
  - 产品名称和版本（从可执行文件版本信息读取）
  - 版权信息（从可执行文件版本信息读取）
  - GitHub 链接（可点击超链接）
- 确定按钮

---

## 串口选择对话框

- 标题: "Select Port"
- 串口下拉列表（自动枚举系统可用串口，显示友好名称，按 COMx 数字自然顺序排序）
- 自动选择上次连接的串口（若存在）
- 显示配置信息: 115200,8N1（默认）
- 确定/取消按钮

---

## 配置持久化

配置文件: `FakeEsptool.ini`（与可执行文件同目录）

### INI 文件格式

```ini
[Font]
Name=Consolas
Size=10
Weight=400

[Port]
LastPort=COM10

[Device]
LastFile=C:\path\to\device.esp
```

### 保存时机

| 配置项 | 保存时机 |
|--------|----------|
| Font | 用户通过字体对话框修改时 |
| LastPort | 成功连接串口时 |
| LastFile | 打开设备文件时、保存设备文件时（新建设备时不清除） |

**LastFile 路径规则**：
- 与应用程序相同盘符时：保存为相对路径（相对于可执行文件目录）
- 不同盘符时：保存为绝对路径
- 加载时：相对路径基于可执行文件目录解析

### 加载时机

| 配置项 | 加载时机 |
|--------|----------|
| Font | 程序启动时 |
| LastPort | 程序启动时（用于Reconnect） |
| LastFile | 程序启动时 |

---

## 启动逻辑

```
程序启动
  │
  ├─ 检测已有实例
  │   │
  │   ├─ 已有实例运行
  │   │   ├─ 有命令行参数（文件路径）
  │   │   │   └─ 提示"程序已运行，是否在已有窗口打开？"
  │   │   │       ├─ Yes → 发送文件路径给已有实例，退出
  │   │   │       └─ No → 退出
  │   │   │
  │   │   └─ 无参数 → 激活已有窗口，退出
  │   │
  │   └─ 无已有实例 → 继续启动
  │
  ├─ 有命令行参数？
  │   │
  │   ├─ Yes → 加载指定文件
  │   │   └─ 加载失败 → 提示错误，创建默认设备
  │   │
  │   └─ No → 读取 LastFile
  │       │
  │       ├─ LastFile 存在？→ 提示"打开上次设备文件？"
  │       │   ├─ Yes → 打开文件
  │       │   └─ No → 创建默认设备
  │       │
  │       └─ 不存在 → 创建默认设备
```

---

## 文件关联与拖放

### 命令行打开文件

支持通过命令行参数指定 .esp 文件路径（支持相对路径，基于当前工作目录）：

```
FakeEsptool.exe <文件路径.esp>
FakeEsptool.exe "C:\path\to\device.esp"
FakeEsptool.exe ".\device.esp"
FakeEsptool.exe "device.esp"
```

**用途**：
- 配置 .esp 文件关联，双击直接打开
- 从其他程序启动并加载指定设备文件

### 拖放打开文件

支持从资源管理器拖放 .esp 文件到程序窗口：

| 场景 | 行为 |
|------|------|
| 拖放单个 .esp 文件 | 提示"打开文件 xxx.esp？" |
| 拖放多个文件 | 提示"只打开第一个文件 xxx.esp，其他文件将被忽略。是否继续？" |
| 拖放非 .esp 文件 | 提示"只支持 .esp 文件" |

**确认后的操作流程**：
1. 检查串口连接 → 提示断开
2. 检查数据修改 → 提示保存
3. 加载设备文件

### 单实例行为

程序采用单实例模式运行：

| 场景 | 行为 |
|------|------|
| 已有实例 + 命令行文件 | 提示是否在已有窗口打开 |
| 已有实例 + 无参数 | 激活已有窗口 |

---

## 串口通信

- **角色**: 设备端（模拟 ESP 芯片）
- **功能**: 响应 esptool 客户端命令
- **波特率**: 115200（默认，支持动态修改）
- **数据位**: 8
- **校验**: 无
- **停止位**: 1
- **读取方式**: WaitCommEvent 事件驱动 + Overlapped I/O

### 下载模式检测

ESP 芯片通过 DTR/RTS 信号控制 GPIO0 和 EN 引脚进入下载模式：

| 信号 | 引脚 | 作用 |
|------|------|------|
| DTR | GPIO0 | 0=低电平, 1=高电平 |
| RTS | EN/RST | 0=低电平, 1=高电平 |

**进入下载模式时序**：
1. DSR:OFF CTS:ON → GPIO0 低
2. DSR:ON CTS:ON → 中间状态
3. DSR:ON CTS:OFF → 复位（EN 低）
4. DSR:OFF CTS:OFF → 进入下载模式

**日志输出**：
- 信号变化：`[SIG] DSR:ON CTS:OFF`
- 进入下载模式：`[SIG] Download mode entered`

### 启动日志

进入下载模式后，设备模拟器输出芯片 ROM bootloader 启动日志（原始 ASCII 文本，非 SLIP 编码）。

**输出顺序**：
```
[SIG] DSR:OFF CTS:OFF
[SIG] Download mode entered
[CFG] Baud rate: 74880          ← 仅 ESP8266 / ESP32-C2(26MHz)
[BOOT] ESP-ROM:esp8684-api2-20220127
[BOOT] Build:Jan 27 2022
[BOOT] rst:0x1 (POWERON),boot:0x4 (DOWNLOAD(UART0))
[BOOT] waiting for download
[CFG] Baud rate: 115200         ← 恢复（仅上述两种情况）
```

**启动波特率规则**：

| 芯片 | XTAL | 启动波特率 |
|------|------|-----------|
| ESP8266 | 任意 | 74880 |
| ESP32-C2 | 26 MHz | 74880 |
| ESP32-C2 | 40 MHz | 115200 |
| 其他 ESP32 | 任意 | 115200 |

**各芯片启动日志格式**：

| 芯片 | ROM 标识 | Boot Mode |
|------|---------|-----------|
| ESP8266 | `ets_main.c 542` / `ets_main.c 543` | `0x3 (DOWNLOAD(UART0/1/2))` |
| ESP32 | `ESP-ROM:esp32-20210719` | `0x3 (DOWNLOAD(UART0/1/2))` |
| ESP32-S2 | `ESP-ROM:esp32s2-20210719` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-S3 | `ESP-ROM:esp32s3-20210719` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-C2 | `ESP-ROM:esp8684-api2-20220127` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-C3 | `ESP-ROM:esp32c3-20210719` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-C6 | `ESP-ROM:esp32c6-20210719` | `0x4 (DOWNLOAD(UART0))` |

---

## esptool 协议

详见 [PROTOCOL.md](PROTOCOL.md)。

### 芯片支持

| 芯片 | 说明 |
|------|------|
| ESP8266 | 经典 WiFi 芯片 |
| ESP32 | 双核 WiFi+BT |
| ESP32-S2 | 单核 WiFi |
| ESP32-S3 | 双核 WiFi+BT5 |
| ESP32-C2 | 低成本 WiFi |
| ESP32-C3 | RISC-V WiFi+BT |
| ESP32-C6 | WiFi 6+BLE 5 |
