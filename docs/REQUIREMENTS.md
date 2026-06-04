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
| New Device... | 创建新设备 | 见状态管理 |
| Open Device... | 加载 .esp 文件 | 见状态管理 |
| Save Device | 保存到当前文件 | 始终可用 |
| Save Device As... | 另存为 | 始终可用 |
| Device Properties... | 修改设备参数 | 未连接时可用，已连接时禁用 |
| Exit | 退出程序 | 见状态管理 |

### Serial 串口菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| Connect | 弹出串口选择对话框 | 已连接时禁用（灰色） |
| Disconnect | 断开当前串口 | 未连接时禁用（灰色） |
| Reconnect | 直接连接上次端口 | 未连接且上次端口可用时启用 |

### Flash 菜单

| 菜单项 | 功能 | 状态逻辑 |
|--------|------|----------|
| Import Flash... | 从 .bin 文件导入 Flash 数据 | 未连接时可用，已连接时禁用 |
| Export Flash... | 导出 Flash 数据到 .bin 文件 | 始终可用 |

**Import Flash 详细要求：**
- 未连接串口时才允许使用
- 检查文件大小与当前 Flash 大小是否一致
- 不一致时显示错误提示

**Export Flash 详细要求：**
- 弹出标准文件保存对话框
- 覆盖已有文件时弹出确认提示

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

### New Device 对话框

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| 芯片类型 | 下拉选择 | ESP8266 | ESP8266/ESP32/S2/S3/C2/C3/C6 |
| 晶振频率 | 下拉选择 | 40MHz | 40MHz/26MHz（ESP32-C3/C6/S2/S3 固定 40MHz 禁用） |
| Flash 大小 | 下拉选择 | 4MB | 256KB/512KB/1MB/2MB/4MB/8MB/16MB |
| MAC 地址 | 输入框 | AA:BB:CC:DD:EE:01 | 支持手动输入和随机生成 |
| 初始 Flash | 单选 | Blank | Blank 或从文件加载 |

### Device Properties 对话框

| 字段 | 类型 | 说明 |
|------|------|------|
| 芯片类型 | 下拉选择 | ESP8266/ESP32/S2/S3/C2/C3/C6 |
| 晶振频率 | 下拉选择 | 40MHz/26MHz（ESP32-C3/C6/S2/S3 固定 40MHz 禁用） |
| Flash 大小 | 下拉选择 | 256KB/512KB/1MB/2MB/4MB/8MB/16MB |
| MAC 地址 | 输入框 | 支持手动输入和随机生成 |

**说明**：修改参数后设备数据标记为已修改，需保存。切换芯片类型时保留原有 Flash 数据（大小不变时）。

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

### 串口连接检查

当串口已连接时执行 New/Open/Exit：

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

当 eFuse 或 Flash 数据被修改后执行 New/Open/Exit：

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
| New Device | 是 |
| Device Properties（修改参数） | 是 |
| eFuse 写入 | 是 |
| Flash 写入 | 是 |
| Flash 擦除 | 是 |
| Flash Import | 是 |
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
| 连接 | toolbar.bmp[3] | Serial/Connect | 已连接时禁用 |
| 重连 | toolbar.bmp[4] | Serial/Reconnect | 上次端口可用时启用 |
| 断开 | toolbar.bmp[5] | Serial/Disconnect | 未连接时禁用 |
| *(分隔符)* | - | - | - |
| 导入 | toolbar.bmp[6] | Flash/Import | 未连接时可用 |
| 导出 | toolbar.bmp[7] | Flash/Export | 始终可用 |
| *(分隔符)* | - | - | - |
| 清除 | toolbar.bmp[8] | Log/Clear | 始终可用 |
| 保存日志 | toolbar.bmp[9] | Log/Save as... | 始终可用 |

**工具栏要求：**
- 使用合并的 BMP 位图（10个16x16彩色图标）
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
- 串口下拉列表（自动枚举系统可用串口，显示友好名称）
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
| LastFile | 打开设备文件时 |

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
  ├─ 读取 LastFile
  │
  ├─ LastFile 存在？
  │   │
  │   ├─ Yes → 提示"打开上次设备文件？"
  │   │         │
  │   │         ├─ Yes → 打开文件 → 进入主界面
  │   │         │
  │   │         └─ No → 弹出 New Device 对话框
  │   │
  │   └─ No → 弹出 New Device 对话框
  │
  └─ New Device 对话框
      │
      ├─ 确定 → 创建新设备 → 进入主界面
      │
      └─ 取消 → 退出程序
```

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

### 协议概述

基于 SLIP 封装的请求/响应协议，模拟 ESP 芯片设备端行为。

### SLIP 封装

- 帧起始/结束: `0xC0`
- 转义 `0xC0`: `0xDB 0xDC`
- 转义 `0xDB`: `0xDB 0xDD`

### 命令格式

请求包:
```
[dir=0x00][cmd][size:2][value:4][data:N][checksum:1]
```

响应包:
```
[dir=0x01][cmd][size:2][status:4][data:N]
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
| 0x13 | SPI_FLASH_MD5 | 计算 Flash MD5 |
| 0x14 | GET_SECURITY_INFO | 获取安全信息 |
| 0xD0 | ERASE_FLASH | 擦除整个 Flash |
| 0xD1 | ERASE_REGION | 擦除 Flash 区域 |
| 0xD2 | READ_FLASH | 读取 Flash |

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
