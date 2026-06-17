---
layout: default
title: FakeEsptool - ESP 芯片设备端模拟器
---

<div style="text-align: center; padding: 60px 20px 40px; background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); color: white; border-radius: 8px; margin-bottom: 40px;">
  <h1 style="font-size: 2.5em; margin-bottom: 10px;">FakeEsptool</h1>
  <p style="font-size: 1.3em; opacity: 0.9; margin-bottom: 30px;">ESP 芯片设备端模拟器 —— 支持虚拟串口的 esptool 协议模拟工具</p>
  <a href="https://github.com/larryli/FakeEsptool/releases" style="display: inline-block; padding: 12px 24px; background: #2ea44f; color: white; text-decoration: none; border-radius: 6px; margin: 0 8px; font-weight: bold;">下载最新版</a>
  <a href="https://github.com/larryli/FakeEsptool" style="display: inline-block; padding: 12px 24px; background: #444d56; color: white; text-decoration: none; border-radius: 6px; margin: 0 8px; font-weight: bold;">GitHub 仓库</a>
</div>

## 功能亮点

<div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 30px 0;">
  <div style="padding: 20px; border: 1px solid #d0d7de; border-radius: 6px; background: #f6f8fa;">
    <h3>芯片模拟</h3>
    <p>支持 ESP8266、ESP32、ESP32-S2/S3、ESP32-C2/C3/C6 全系列芯片模拟，含 eFuse 和启动日志。</p>
  </div>
  <div style="padding: 20px; border: 1px solid #d0d7de; border-radius: 6px; background: #f6f8fa;">
    <h3>完整协议</h3>
    <p>esptool SLIP 协议完整响应，支持 Flash 烧录（普通/压缩/加密）、读取、擦除、MD5 校验。</p>
  </div>
  <div style="padding: 20px; border: 1px solid #d0d7de; border-radius: 6px; background: #f6f8fa;">
    <h3>加密功能</h3>
    <p>AES-XTS 加密烧录，安全启动、密钥管理，支持无加密/开发模式/生产模式切换。</p>
  </div>
  <div style="padding: 20px; border: 1px solid #d0d7de; border-radius: 6px; background: #f6f8fa;">
    <h3>虚拟串口</h3>
    <p>支持 com0com 虚拟串口和实体 USB 转串口，DTR/RTS 信号控制，动态波特率切换。</p>
  </div>
  <div style="padding: 20px; border: 1px solid #d0d7de; border-radius: 6px; background: #f6f8fa;">
    <h3>设备管理</h3>
    <p>新建、打开、保存 .esp 设备文件，修改芯片属性，导入/导出 Flash 内容。</p>
  </div>
  <div style="padding: 20px; border: 1px solid #d0d7de; border-radius: 6px; background: #f6f8fa;">
    <h3>多语言界面</h3>
    <p>支持英文和简体中文，实时 HEX 日志显示，工具栏快捷操作，配置自动持久化。</p>
  </div>
</div>

## 快速开始

1. **准备串口** — 使用 com0com 创建虚拟串口对（如 COM10 ↔ COM11），或用两个 USB 转串口适配器连线
2. **运行程序** — 启动 `FakeEsptool.exe`，自动创建默认 ESP32 设备
3. **配置设备** — 通过 `File > Device Properties` 修改芯片类型、Flash 大小等参数
4. **连接串口** — `Serial > Connect` 选择串口
5. **开始烧录** — 用 esptool 客户端连接另一端进行烧录测试

## 系统要求

- **操作系统：** Windows 10/11 (x64)
- **编译环境：** CMake 3.20+ / MSVC **或** Pelles C 14.10+

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

<div style="text-align: center; padding: 30px; color: #656d76; margin-top: 40px;">
  <p>基于 MIT 许可证开源 | Copyright &copy; 2025 - 2026 Larry Li</p>
</div>
