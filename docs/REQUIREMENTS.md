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
- 默认文件名格式: `SerialEcho_YYYYMMDD_HHMMSS.log`
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
| 芯片类型 | 下拉选择 | ESP8266 | ESP8266/ESP32/S2/S3/C2/C3/C6/C61/H2 |
| Flash 大小 | 下拉选择 | 4MB | 256KB/512KB/1MB/2MB/4MB/8MB/16MB |
| MAC 地址 | 输入框 | AA:BB:CC:DD:EE:01 | 支持手动输入和随机生成 |
| Flash 模式 | 下拉选择 | DIO | QIO/QOUT/DIO/DOUT |
| Flash 频率 | 下拉选择 | 40MHz | 20/26/40/80MHz |
| 初始 Flash | 单选 | Blank | Blank 或从文件加载 |

### 设备文件格式

- 文件扩展名: `.esp`
- 二进制格式，包含芯片类型、MAC、eFuse、完整 Flash 数据

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
| 连接 | toolbar.bmp[0] | Serial/Connect | 已连接时禁用 |
| 重连 | toolbar.bmp[0] | Serial/Reconnect | 上次端口可用时启用 |
| *(分隔符)* | - | - | - |
| 断开 | toolbar.bmp[1] | Serial/Disconnect | 未连接时禁用 |
| *(分隔符)* | - | - | - |
| 清除 | toolbar.bmp[2] | Log/Clear | 始终可用 |
| 保存 | toolbar.bmp[3] | Log/Save as... | 始终可用 |

**工具栏要求：**
- 使用合并的 BMP 位图（4个16x16图标）
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

---

## esptool 协议

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
| 0x08 | SYNC | 同步握手 |
| 0x09 | SPI_SET_PARAMS | 设置 Flash 参数 |
| 0x0A | READ_REG | 读寄存器 |
| 0x0B | WRITE_REG | 写寄存器（eFuse） |
| 0x0D | SPI_ATTACH | 附加 SPI |
| 0x0F | CHANGE_BAUDRATE | 修改波特率 |
| 0x13 | SPI_FLASH_MD5 | 计算 Flash MD5 |
| 0x14 | SPI_READ_FLASH | 读取 Flash |
| 0x15 | SPI_ERASE_FLASH | 擦除 Flash |
| 0x16 | SPI_ERASE_BLOCK | 擦除块 |
| 0x20 | FLASH_DEFL_BEGIN | 压缩写入开始 |
| 0x21 | FLASH_DEFL_DATA | 压缩写入数据 |
| 0x22 | FLASH_DEFL_END | 压缩写入结束 |
| 0x23 | FLASH_DEFL_MD5 | 压缩写入 MD5 |

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
| ESP32-C61 | WiFi 6+BLE 5 |
| ESP32-H2 | BLE 5+Zigbee/Thread |
