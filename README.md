# FakeEsptool

ESP 芯片设备端模拟器 —— 支持虚拟串口的 esptool 协议模拟工具。

## 功能概述

- ESP 芯片设备模拟（支持 ESP8266/ESP32/S2/S3/C2/C3/C6）
- esptool SLIP 协议响应（读写 Flash、eFuse 等）
- DEFLATE 压缩模式烧录支持（压缩数据自动解压）
- 实时 HEX 日志显示（带时间戳和颜色）
- 设备文件管理（新建、打开、保存 .esp 文件）
- Flash 导入/导出（.bin 文件）
- 设备属性修改
- 配置持久化（字体、端口、设备文件）

## 系统要求

- Windows 10/11 (x64)
- CMake 3.20+ / MSVC 编译器

## 编译

```powershell
cmake -S . -B build -G "NMake Makefiles"
cmake --build build
```

选项：
- `-DENABLE_TRACE_FW=ON` 启用框架调试日志
- `-DENABLE_TRACE_PROTO=ON` 启用协议调试日志

## 使用

1. 创建虚拟串口对（使用 com0com 或其他虚拟串口驱动）
2. 运行 `build\FakeEsptool.exe`
3. File > New Device 创建新设备，或 File > Open Device 加载已有设备
4. Serial > Connect 选择串口
5. 用 esptool 客户端连接另一端进行烧录测试

## 项目结构

```
FakeEsptool/
├── src/                        # 源代码
│   ├── main.c / main.h         # 程序入口和 GUI 实现
│   ├── serial.c / serial.h     # 串口通信模块
│   ├── resource.h              # 资源 ID
│   ├── resource.rc             # 资源文件（菜单、对话框、字符串）
│   ├── esptool/                # esptool 协议模块
│   │   ├── slip.c / slip.h     # SLIP 协议编解码
│   │   ├── chip.c / chip.h     # 芯片特性模拟
│   │   ├── flash.c / flash.h   # Flash 存储模拟
│   │   ├── device.c / device.h # 设备文件管理
│   │   └── esptool.c / esptool.h # 命令解析与响应
│   ├── utils/                  # 辅助模块
│   │   ├── config.c / config.h # 配置持久化
│   │   ├── lang.c / lang.h     # 国际化辅助
│   │   ├── timer.c / timer.h   # 定时器工具
│   │   ├── trace.c / trace.h   # 调试日志
│   │   └── deflate.c / deflate.h # DEFLATE 解压器
│   └── res/                    # 资源文件（图标、位图、清单）
├── tests/                      # 测试
│   ├── test_deflate.c          # DEFLATE 解压器测试
│   ├── test_data.h             # 测试数据（Python zlib 生成）
│   ├── generate_test_data.py   # 测试数据生成脚本
│   └── CMakeLists.txt          # 测试构建配置
├── docs/                       # 文档
│   ├── REQUIREMENTS.md         # 需求规格
│   ├── DEVELOPMENT.md          # 开发文档
│   ├── PROTOCOL.md             # 协议规范
│   └── TODO.md                 # 待办改进项
├── LICENSE                     # MIT 许可证
└── README.md
```

## 文档

- [需求规格说明](docs/REQUIREMENTS.md)
- [开发文档](docs/DEVELOPMENT.md)
- [协议规范](docs/PROTOCOL.md)

## 致谢

本项目使用 AI 辅助开发，感谢以下 AI 助手的贡献：

- **OpenCode** - 由 [Anomaly](https://github.com/anomalyco) 开发的交互式 CLI 工具，基于小米大模型（mimo-v2.5-pro），在代码分析、架构设计、协议实现等方面提供了重要支持。

## 许可证

本项目采用 [MIT 许可证](LICENSE)。

Copyright (c) 2025 - 2026 Larry Li
