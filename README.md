# FakeEsptool

ESP 芯片设备端模拟器 —— 支持虚拟串口的 esptool 协议模拟工具。

## 功能概述

**芯片模拟**
- 支持 ESP8266、ESP32、ESP32-S2、ESP32-S3、ESP32-C2、ESP32-C3、ESP32-C5、ESP32-C6、ESP32-C61、ESP32-H2、ESP32-P4、ESP32-S31
- eFuse 一次性可编程模拟（OR 写入行为）
- 启动日志输出（各芯片 ROM Bootloader 格式）

**协议支持**
- esptool SLIP 协议完整响应
- Flash 烧录（普通/压缩/加密模式）
- Flash 读取、擦除、MD5 校验
- 内存写入（Stub 上传）
- 寄存器读写、波特率切换

**加密功能**
- Flash 加密烧录（AES-XTS 算法）
- 加密状态控制（无加密/开发模式/量产模式）
- 下载模式控制（正常/安全/禁用）
- 密钥管理（导入/导出/生成）
- 安全启动、JTAG 状态显示

**设备管理**
- 新建、打开、保存 .esp 设备文件
- 设备属性修改（芯片类型、晶振频率、Flash 大小、MAC 地址）
- Flash 导入/导出（.bin 文件）
- 设备内容导出（txt 文件）

**串口通信**
- 虚拟串口（com0com）和实体串口支持
- DTR/RTS 信号控制（下载模式检测）
- 动态波特率切换

**用户界面**
- 多语言支持（英文、简体中文）
- 实时 HEX 日志显示（带时间戳和颜色）
- 工具栏快捷操作
- 状态栏信息显示（加密状态、下载模式、安全启动、JTAG）
- 配置持久化（字体、端口、设备文件）

**文件操作**
- 命令行打开文件（支持 .esp 文件关联）
- 拖放文件到窗口打开
- 单实例模式（避免串口冲突）

## 系统要求

- Windows 10/11 (x64)
- CMake 3.20+ / MSVC 编译器 **或** Pelles C 14.10+

## 编译

### CMake + MSVC

```powershell
# 配置
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -B build

# 编译
cmake --build build --config Release -j
```

选项：
- `-DENABLE_TRACE_FW=ON` 启用框架调试日志
- `-DENABLE_TRACE_PROTO=ON` 启用协议调试日志

输出：`build/Release/FakeEsptool.exe`

### Pelles C

```powershell
# 编译
pomake /f FakeEsptool.ppj
```

输出：`FakeEsptool.exe`

## 使用

### 快速开始

1. 创建虚拟串口对或连接实体串口（见下方详细说明）
2. 运行 `build\Release\FakeEsptool.exe`（自动创建默认 ESP32 设备）
3. 如需修改设备参数，使用 File > Device Properties
4. Serial > Connect 选择串口
5. 用 esptool 客户端连接另一端进行烧录测试

### 串口连接方式

#### 方式一：com0com 虚拟串口对

com0com 是 Windows 平台免费的虚拟串口驱动，创建成对的虚拟串口（如 COM10 ↔ COM11）。

**注意事项：**

- com0com 虚拟串口支持 DTR/RTS 信号控制，可测试完整的下载模式进入流程
- **WSL 兼容性警告：** com0com 创建的虚拟串口在 WSL (Windows Subsystem for Linux) 中**无法使用**。WSL 无法访问 Windows 原生的 com0com 虚拟串口设备。如需在 WSL 中使用 esptool，请使用实体串口或支持 WSL 的其他虚拟串口方案。

#### 方式二：实体串口对（USB 转串口）

使用两个 USB 转串口适配器，通过物理连线创建串口对。

**所需设备：**

- 2 个 USB 转串口适配器（如 CH340、CP2102、FT232）
- 杜邦线 2 根或 4 根（同一电脑 GND 已共地）

**最小连接（2 线）：**

仅需 TXD、RXD 两根线即可通信，但无法检测 DTR/RTS 信号：

```
  USB 串口 A          USB 串口 B
  ┌────────┐         ┌────────┐
  │  TXD ──┼─────────┼─► RXD  │
  │  RXD ◄─┼─────────┼── TXD  │
  └────────┘         └────────┘
```

由于缺少信号线，FakeEsptool 无法检测 DTR/RTS 信号，因此：
- 无法输出启动日志（Boot Message）——即使断开重连也不会触发
- 无法自动清除 eFuse 易失性数据——断开重连可触发此清除

**每次通讯完成后，请手动断开再重新连接串口**，以模拟 RESET 行为并触发易失数据清除。

**完整连接（含信号控制）：**

支持信号检测的完整连线：

```
  烧录器端 (esptool)         模拟设备端 (FakeEsptool)
  ┌────────┐                 ┌────────┐
  │  TXD ──┼─────────────────┼─► RXD  │
  │  RXD ◄─┼─────────────────┼── TXD  │
  │  DTR ──┼─────────────────┼─► DSR  │
  │  RTS ──┼─────────────────┼─► CTS  │
  └────────┘                 └────────┘
```

当然，也可以使用 6 根连线连接，就无需区分方向。

## 项目结构

```
FakeEsptool/
├── src/                        # 源代码（具体请参考开发文档）
├── tests/                      # 测试代码（具体请参考开发文档）
├── tools/                      # 工具
│   └── verify_flash.py         # 烧录验证工具
├── docs/                       # 文档
│   ├── REQUIREMENTS.md         # 需求规格
│   ├── DEVELOPMENT.md          # 开发文档
│   ├── API.md                  # API 参考
│   ├── PROTOCOL.md             # 协议规范
│   ├── WRITE_FLASH_GUIDE.md    # 烧录流程指南
│   ├── ENCRYPT_FLASH_GUIDE.md  # 加密烧录指南
│   └── TODO.md                 # 待办改进项
├── CHANGELOG.md                # 变更记录
├── LICENSE                     # MIT 许可证
└── README.md
```

## 文档

- [需求规格说明](docs/REQUIREMENTS.md)
- [开发文档](docs/DEVELOPMENT.md)
- [API 参考](docs/API.md)
- [协议规范](docs/PROTOCOL.md)
- [烧录流程指南](docs/WRITE_FLASH_GUIDE.md)
- [加密烧录指南](docs/ENCRYPT_FLASH_GUIDE.md)
- [变更记录](CHANGELOG.md)
- [待办改进项](docs/TODO.md)

## 致谢

本项目使用 AI 辅助开发，感谢以下 AI 助手的贡献：

- **OpenCode** - 由 [Anomaly](https://github.com/anomalyco) 开发的交互式 CLI 工具，基于小米大模型（mimo-v2.5-pro），在代码分析、架构设计、协议实现等方面提供了重要支持。
- **Mimo Code** - 由 [Xiaomi MiMo Team](https://github.com/XiaomiMiMo) 开发的 AI 编程助手，基于 mimo-auto 模型，持续辅助本项目的开发与维护工作。

## 许可证

本项目采用 [MIT 许可证](LICENSE)。

Copyright (c) 2025 - 2026 Larry Li
