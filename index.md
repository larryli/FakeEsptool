---
layout: default
title: FakeEsptool - ESP 芯片设备端模拟器
---

## FakeEsptool 是什么

ESP 芯片设备端模拟器，支持虚拟串口。模拟 ESP8266/ESP32 系列芯片，响应 esptool 客户端的烧录协议。

无需真实硬件，即可测试 ESP 芯片的烧录、加密、安全启动等功能。

[功能说明](features) | [ESP 加密烧录](encryption) | [快速烧录指南](quick-start) | [GitHub 仓库](https://github.com/larryli/FakeEsptool)

---

## 核心功能

| 功能 | 说明 |
|------|------|
| **芯片模拟** | ESP8266、ESP32、ESP32-S2/S3、ESP32-C2/C3/C6 全系列 |
| **完整协议** | esptool SLIP 协议完整响应，支持 Flash 烧录、读取、擦除、MD5 校验 |
| **加密烧录** | AES-XTS 加密，支持开发/生产模式，详细说明 → [ESP 加密烧录](encryption) |
| **虚拟串口** | com0com 虚拟串口和实体 USB 转串口，DTR/RTS 信号控制 |
| **设备管理** | 新建、打开、保存 .esp 设备文件，导入/导出 Flash |

---

## 快速开始

1. **准备串口** — 使用 com0com 创建虚拟串口对（如 COM10 ↔ COM11）
2. **运行程序** — 启动 `FakeEsptool.exe`，自动创建默认 ESP32 设备
3. **配置设备** — 通过 `File > Device Properties` 修改芯片类型、Flash 大小等参数
4. **连接串口** — `Serial > Connect` 选择串口
5. **开始烧录** — 用 esptool 客户端连接另一端进行烧录测试

详细烧录流程 → [快速烧录指南](quick-start)

---

## 系统要求

- **操作系统：** Windows 10/11 (x64)
- **编译环境：** CMake 3.20+ / MSVC **或** Pelles C 14.10+

---

## 编译

### CMake + MSVC

```powershell
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -B build
cmake --build build --config Release -j
```

输出：`build/Release/FakeEsptool.exe`

### Pelles C

```powershell
pomake /f FakeEsptool.ppj
```

输出：`FakeEsptool.exe`

---

## 文档导航

| 文档 | 说明 |
|------|------|
| [功能说明](features) | 程序功能概述、设计原则 |
| [ESP 加密烧录](encryption) | Flash 加密原理、eFuse 说明、各芯片测试流程 |
| [快速烧录指南](quick-start) | esptool write-flash 流程详解、日志解读 |

---

[下载最新版](https://github.com/larryli/FakeEsptool/releases) | [GitHub 仓库](https://github.com/larryli/FakeEsptool)

基于 MIT 许可证开源 | Copyright © 2025 - 2026 Larry Li
