# FakeEsptool - 需求规格说明

## 项目概述

ESP 芯片设备端模拟器，支持虚拟串口。程序模拟 ESP8266/ESP32 系列芯片，响应 esptool 客户端的烧录协议。

- **技术栈**: C + CMake + Win32 API
- **波特率**: 115200, 8N1（默认，支持动态修改）
- **串口选择**: 弹出对话框手动选择
- **协议**: esptool SLIP 协议

---

## 设计原则

程序分为两层，职责明确：

- **协议层**（`fesptool/*.c`）：严格模拟真实设备行为。eFuse 只能 OR 写入（模拟一次性可编程特性），串口信号检测、命令响应、状态机等完全遵循真实 ROM/stub 行为。客户端（esptool/espefuse）通过此层看到的设备行为与真实芯片一致。
- **GUI 层**（`app_commands.c`、`dlg/*.c`）：提供高级测试功能。可直接修改 eFuse 标志位（支持清除操作）、导入导出密钥等，方便构造特定测试场景（如加密烧录、安全启动等）。

两层互不干扰：GUI 的 eFuse 修改不经过协议层的 eFuse 控制器模拟，直接操作内存中的 eFuse 数组。

---

## 菜单结构

### Device 设备菜单

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

### Storage 存储菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| Import Flash... | 从 .bin 文件导入 Flash 数据 | 见状态管理 |
| Export Flash... | 导出 Flash 数据到 .bin 文件 | 始终可用 |
| Key Management... | 打开密钥管理对话框 | ESP8266 禁用（不支持 Flash 加密），连接时提示断开 |
| Encryption State ► | 切换加密状态（单选） | 始终可用，不影响串口和 Flash 数据 |
| Download Mode ► | 切换下载模式（单选） | 始终可用，不影响串口和 Flash 数据 |

**Encryption State 子菜单**：

| 菜单项 | 说明 | 选中条件 |
|--------|------|----------|
| No Encryption | 未加密 | 默认选中 |
| Encrypted (Dev) | 加密（开发模式） | - |
| Encrypted (Release) | 加密（量产模式） | - |

**Download Mode 子菜单**：

| 菜单项 | 说明 | 选中条件 |
|--------|------|----------|
| Download Normal | 下载正常 | 默认选中 |
| Download Secure | 安全下载模式 | - |
| Download Disabled | 下载禁用 | - |

**说明**：
- 加密状态和下载模式切换直接修改 eFuse 数组（支持清除位，非真实 OR 行为），属于 GUI 层高级功能
- 切换后设备标记为已修改（`modified=TRUE`），需保存
- 可在串口连接状态下实时切换，方便测试
- 切换后状态栏和 Tooltip 立即更新显示

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
| 芯片类型 | 下拉选择 | ESP8266/ESP32/S2/S3/C2/C3/C6/C5/C61/H2/P4/S31 |
| 晶振频率 | 下拉选择 | 40MHz/26MHz/48MHz（ESP32-C3/C6/S2/S3/C61/P4/S31 固定 40MHz；ESP32-H2 固定 32MHz；ESP8266/ESP32/C2 可选 40/26MHz；ESP32-C5 可选 40/48MHz，均禁用） |
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

| 芯片 | eFuse 块数量 | 密钥块 | 密钥长度 | 说明 |
|------|-------------|--------|---------|------|
| ESP32 | 4 个 (BLOCK0-3) | BLOCK1-3 (3个) | 256-bit | BLOCK1=Flash加密, BLOCK2=安全启动, BLOCK3=用户数据 |
| ESP32-S2/S3 | 多个 | KEY0-KEY5 (6个) | 256-bit | 统一密钥管理器，支持用途标记 |
| ESP32-C2 | 4 个 (BLOCK0-3) | BLOCK_KEY0 (1个) | 256-bit | 只有 BLOCK_KEY0 用于密钥存储 |
| ESP32-C3/C6 | 多个 | KEY0-KEY5 (6个) | 256-bit | 标准新架构，完整支持安全启动 v2 |
| ESP32-C5/C61/H2/P4 | 多个 | KEY0-KEY5 (6个) | 256-bit | 同 C3/C6 架构 |
| ESP32-S31 | 多个 | KEY0-KEY4 (5个) | 256-bit | KEY_PURPOSE 5-bit 字段 |

**按钮行为**：

| 按钮 | 功能 | 说明 |
|------|------|------|
| Import | 从文件导入密钥 | 弹出文件选择对话框，选择 .bin 文件 |
| Export | 导出密钥到文件 | 弹出文件保存对话框，保存为 .bin 文件 |
| Generate | 生成随机密钥 | 生成随机密钥并写入选中的密钥块 |
| Close | 关闭对话框 | 如有修改，标记设备为已修改 |

**状态逻辑**：
- ESP8266 不支持 Flash 加密，菜单项和工具栏按钮禁用（灰色）
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
| Encryption State 菜单切换 | 是 |
| Download Mode 菜单切换 | 是 |
| eFuse 写入（协议层） | 是 |
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

| 栏位 | 内容 | 宽度 | Tooltip |
|------|------|------|---------|
| 第1栏 | 芯片类型 + Flash 大小（如 `ESP32 4MB`） | 固定 100px | XTAL 频率 + MAC 地址（如 `40MHz AA:BB:CC:DD:EE:01`） |
| 第2栏 | 加密状态 | 固定 110px | eFuse 字段值（`SPI_BOOT_CRYPT_CNT` + `DIS_DOWNLOAD_MANUAL_ENCRYPT`） |
| 第3栏 | 下载模式 | 固定 110px | eFuse 字段值（`DIS_DOWNLOAD_MODE` 等） |
| 第4栏 | 安全启动状态 | 固定 110px | eFuse 字段值（`SECURE_BOOT_EN` 或 `ABS_DONE_0` + `ABS_DONE_1`） |
| 第5栏 | JTAG 状态 | 固定 100px | eFuse 字段值（所有 JTAG 相关字段） |
| 第6栏 | 端口 + 串口配置（如 `COM10 115200,8N1`） | 剩余空间 | 无 |

**不支持的字段不显示 Tooltip。ESP8266 和 ESP32-C2 不支持的字段无 Tooltip。**

**安全启动显示**：

| 状态 | 英文 | 中文 | 判断逻辑 |
|------|------|------|----------|
| 未启用 | `No Secure Boot` | `无安全启动` | 所有安全启动位均为 0 |
| 已启用 | `Secure Boot` | `安全启动` | 任一安全启动位为 1 |

**安全启动 eFuse 字段（Tooltip）**：

| 芯片 | 字段 |
|------|------|
| ESP8266/C2 | 不支持，无 Tooltip |
| ESP32 | `ABS_DONE_0`（V1）+ `ABS_DONE_1`（V2） |
| ESP32-S2/S3/C3/C6 | `SECURE_BOOT_EN` |

**JTAG 状态显示**：

| 状态 | 英文 | 中文 | 判断逻辑 |
|------|------|------|----------|
| 可用 | `JTAG Enabled` | `JTAG 可用` | 所有 JTAG 接口均未禁用 |
| 部分可用 | `JTAG Partial` | `JTAG 部分可用` | 部分 JTAG 接口已禁用 |
| 全部禁用 | `JTAG Disabled` | `JTAG 已禁用` | 所有 JTAG 接口均已禁用 |

**JTAG eFuse 字段（Tooltip）**：

| 芯片 | 字段数 | 字段 |
|------|--------|------|
| ESP8266/C2 | 0 | 不支持，无 Tooltip |
| ESP32 | 1 | `JTAG_DISABLE` |
| ESP32-S2 | 2 | `DIS_PAD_JTAG` + `SOFT_DIS_JTAG` |
| ESP32-S3/C3/C6 | 3 | `DIS_PAD_JTAG` + `SOFT_DIS_JTAG` + `DIS_USB_JTAG` |

**加密状态显示**：

| 状态 | 英文 | 中文 |
|------|------|------|
| 无加密 | `No Encryption` | `未加密` |
| 开发模式 | `Encrypted (Dev)` | `加密（开发）` |
| 量产模式 | `Encrypted (Release)` | `加密（量产）` |

**下载模式显示**：

| 状态 | 英文 | 中文 |
|------|------|------|
| 正常 | `Download Normal` | `下载正常` |
| 安全模式 | `Download Secure` | `下载安全` |
| 已禁用 | `Download Disabled` | `下载禁用` |

**下载模式判断规则**：

| 芯片 | 禁用下载标志 | 安全下载标志 | 判断逻辑 |
|------|-------------|-------------|----------|
| ESP8266 | 无 | 无 | 始终 `Download Normal` |
| ESP32 | `UART_DOWNLOAD_DIS` | 无 | `UART_DOWNLOAD_DIS=1` → `Download Disabled`，否则 `Download Normal` |
| ESP32-S2 | `DIS_USB_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | `DIS_USB_DOWNLOAD_MODE=1` → `Download Disabled`，`ENABLE_SECURITY_DOWNLOAD=1` → `Download Secure`，否则 `Download Normal` |
| ESP32-S3 | `DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | `DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE=1` → `Download Disabled`，`ENABLE_SECURITY_DOWNLOAD=1` → `Download Secure`，否则 `Download Normal` |
| ESP32-C2 | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | `DIS_DOWNLOAD_MODE=1` → `Download Disabled`，`ENABLE_SECURITY_DOWNLOAD=1` → `Download Secure`，否则 `Download Normal` |
| ESP32-C3 | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | `DIS_DOWNLOAD_MODE=1` → `Download Disabled`，`ENABLE_SECURITY_DOWNLOAD=1` → `Download Secure`，否则 `Download Normal` |
| ESP32-C6 | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | `DIS_DOWNLOAD_MODE=1` → `Download Disabled`，`ENABLE_SECURITY_DOWNLOAD=1` → `Download Secure`，否则 `Download Normal` |

**ESP8266 菜单限制**：ESP8266 不支持加密和下载模式控制，Storage > Encryption State 和 Storage > Download Mode 子菜单全部禁用（灰色）。

**ESP32 菜单限制**：ESP32 不支持安全下载模式，Storage > Download Mode > Download Secure 菜单项禁用（灰色）。

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
| ESP32 | `ets Jun  8 2016 00:22:57` | `0x3 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))` |
| ESP32-S2 | `ESP-ROM:esp32s2-rc4-20191025` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-S3 | `ESP-ROM:esp32s3-20210327` | `0x0 (DOWNLOAD(USB/UART0))` |
| ESP32-C2 | `ESP-ROM:esp32c2-eco4-20240515` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-C3 | `ESP-ROM:esp32c3-api1-20210207` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-C5 | `ESP-ROM:esp32c5-eco2-20250121` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-C6 | `ESP-ROM:esp32c6-20220919` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-C61 | `ESP-ROM:esp32c61-eco3-20250228` | `0x4 (DOWNLOAD(UART0))` |
| ESP32-H2 | `ESP-ROM:esp32h2-20221101` | `0x4 (DOWNLOAD(USB/UART0))` |
| ESP32-P4 | `ESP-ROM:esp32p4-eco1-20240205` | `0x107 (DOWNLOAD(USB/UART0/SPI))` |
| ESP32-S31 | `ESP-ROM:esp32s31-20251218` | `0x69 (DOWNLOAD(USB/UART0/SPI))` |

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
| ESP32-C5 | WiFi 6 双频+BT5+802.15.4, RISC-V 单核+LP核 240MHz |
| ESP32-C61 | WiFi 6+BT5, RISC-V 单核 160MHz |
| ESP32-H2 | BT5+802.15.4, RISC-V 单核 96MHz |
| ESP32-P4 | 高性能 MCU, 双核+LP核 400MHz, 无无线 |
| ESP32-S31 | WiFi 6+BT5.4+802.15.4, 双核+LP核 300MHz |
