# FakeEsptool

串口设备模拟器 —— 基于 com0com 虚拟串口驱动的数据回环工具。

## 功能概述

- 串口回环测试（接收数据原样返回）
- 实时 HEX 日志显示（带时间戳和颜色）
- Ping 测试（发送随机数据）
- 可扩展的协议处理框架
- 字体配置与 INI 持久化

## 系统要求

- Windows 10/11 (x64)
- [com0com](https://sourceforge.net/projects/com0com/) 虚拟串口驱动
- CMake 3.20+ / MSVC 编译器

## 编译

```powershell
mkdir build && cd build
cmake .. -G "NMake Makefiles"
cmake --build .
```

选项：
- `-DENABLE_TRACE_FW=ON` 启用框架调试日志
- `-DENABLE_TRACE_PROTO=ON` 启用协议调试日志
- `-DCMAKE_BUILD_TYPE=Debug` 调试构建

## 使用

1. 安装 com0com，创建虚拟串口对
2. 运行 `build\FakeEsptool.exe`
3. Serial > Connect 选择端口
4. 用串口工具连接另一端，发送数据测试

## 项目结构

```
FakeEsptool/
├── src/                    # 源代码
│   ├── main.c / main.h     # 程序入口和 GUI 实现
│   ├── serial.c / serial.h # 串口通信模块
│   ├── protocol.c / protocol.h # 协议处理模块
│   ├── resource.h          # 资源 ID
│   ├── resource.rc         # 资源文件（菜单、对话框、字符串）
│   └── utils/              # 辅助模块
│       ├── config.c / config.h   # 配置持久化
│       ├── lang.c / lang.h       # 国际化辅助
│       ├── timer.c / timer.h     # 定时器工具
│       └── trace.c / trace.h     # 调试日志
├── res/                    # 资源文件（图标、位图、清单）
├── docs/                   # 文档
│   ├── REQUIREMENTS.md     # 需求规格
│   └── DEVELOPMENT.md      # 二次开发指南
├── LICENSE                 # MIT 许可证
└── README.md
```

## 文档

- [需求规格说明](docs/REQUIREMENTS.md)
- [二次开发指南](docs/DEVELOPMENT.md)

## 许可证

本项目采用 [MIT 许可证](LICENSE)。

Copyright (c) 2026 Larry Li
