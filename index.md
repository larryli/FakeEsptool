---
layout: default
title: FakeEsptool - ESP 芯片设备端模拟器
---

## FakeEsptool 是什么

ESP 芯片设备端模拟器，支持虚拟串口。模拟 ESP8266/ESP32 系列芯片，响应 esptool 客户端的烧录协议。

无需真实硬件，即可测试 ESP 芯片的烧录、加密、安全启动等功能。

- **技术栈**: C + CMake + Win32 API
- **协议**: esptool SLIP 协议
- **平台**: Windows 10/11 (x64)

[ESP 加密烧录](encryption) | [快速烧录指南](quick-start) | [GitHub 仓库](https://github.com/larryli/FakeEsptool)

---

## 核心功能

### 芯片模拟

支持全系列 ESP 芯片模拟：

| 芯片 | 类型 | Flash 加密 | 安全启动 |
|------|------|-----------|---------|
| ESP8266 | 经典 WiFi | ❌ | ❌ |
| ESP32 | 双核 WiFi+BT | ✅ | ✅ |
| ESP32-S2 | 单核 WiFi | ✅ | ✅ |
| ESP32-S3 | 双核 WiFi+BT5 | ✅ | ✅ |
| ESP32-C2 | 低成本 WiFi | ✅ | ❌ |
| ESP32-C3 | RISC-V WiFi+BT | ✅ | ✅ |
| ESP32-C6 | WiFi 6+BLE 5 | ✅ | ✅ |

### 加密烧录

完整模拟 ESP 芯片的 Flash 加密功能：

- **eFuse 模拟**：一次性可编程存储，OR 写入特性
- **密钥管理**：支持 256-bit / 512-bit 密钥导入、导出、生成
- **加密模式**：开发模式 / 量产模式 (Release)
- **加密状态**：通过菜单或 eFuse 写入切换

详细说明 → [ESP 加密烧录](encryption)

### 虚拟串口

支持 com0com 虚拟串口和实体 USB 转串口：

- DTR/RTS 信号控制
- 动态波特率切换
- 事件驱动读取

### 设备管理

- 新建、打开、保存 .esp 设备文件
- 修改芯片类型、Flash 大小等参数
- 导入/导出 Flash 内容
- 导出设备 Dump（eFuse + Flash）

### 完整协议

esptool SLIP 协议完整响应，支持 Flash 烧录、读取、擦除、MD5 校验

详细烧录流程 → [快速烧录指南](quick-start)

---

## 用户界面

### 主窗口

- 彩色日志显示（RX/TX/信号/配置/启动日志）
- HEX 数据格式化（每 8 字节分组，16 字节换行）
- 状态栏实时显示：芯片类型、加密状态、下载模式、安全启动、JTAG 状态

### 工具栏

快捷操作：新建、打开、保存、设备属性、连接、重连、断开、导入、导出、清除日志、保存日志

### 菜单

- **Device**：设备管理
- **Serial**：串口连接
- **Storage**：Flash 导入导出、密钥管理、加密/下载模式切换
- **Log**：日志管理
- **Help**：关于

---

## 设计原则

程序分为两层，职责明确：

- **协议层**：严格模拟真实设备行为。eFuse 只能 OR 写入，串口信号检测、命令响应、状态机等完全遵循真实 ROM/stub 行为。
- **GUI 层**：提供高级测试功能。可直接修改 eFuse 标志位（支持清除操作）、导入导出密钥等，方便构造特定测试场景。

两层互不干扰：GUI 的 eFuse 修改不经过协议层的 eFuse 控制器模拟，直接操作内存中的 eFuse 数组。

---

## 快速开始

1. **准备串口** — 使用 com0com 创建虚拟串口对（如 COM10 ↔ COM11）
2. **运行程序** — 启动 `FakeEsptool.exe`，自动创建默认 ESP32 设备
3. **配置设备** — 通过 `File > Device Properties` 修改芯片类型、Flash 大小等参数
4. **连接串口** — `Serial > Connect` 选择串口
5. **开始烧录** — 用 esptool 客户端连接另一端进行烧录测试

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

## 文档

| 文档 | 说明 |
|------|------|
| [ESP 加密烧录](encryption) | Flash 加密原理、eFuse 说明、各芯片测试流程 |
| [快速烧录指南](quick-start) | esptool write-flash 流程详解、日志解读 |

---

[下载最新版](https://github.com/larryli/FakeEsptool/releases) | [GitHub 仓库](https://github.com/larryli/FakeEsptool)

基于 MIT 许可证开源 | Copyright © 2025 - 2026 Larry Li
