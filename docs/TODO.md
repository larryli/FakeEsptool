# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 低优先级 - 成熟芯片支持

支持 esptool 官方已完善支持的 ESP 芯片。

| 芯片 | 特性 | eFuse基址 | SPI基址 | 晶振 |
|------|------|-----------|---------|------|
| ESP32-C5 | WiFi 6双频+BT5+802.15.4, RISC-V单核240MHz | 0x600B4800 | 0x60002000 | 40/48MHz |
| ESP32-C61 | WiFi 6+BT5, RISC-V单核 | 0x600B4800 | 0x60002000 | 40MHz |
| ESP32-H2 | BT5+802.15.4, RISC-V单核96MHz | 0x600B0800 | 0x60002000 | 32MHz |
| ESP32-P4 | 高性能MCU, 无无线 | 0x5012D000 | 0x5008D000 | 40MHz |

**实现内容：**
- chip.h: 添加 CHIP_ESP32C5、CHIP_ESP32C61、CHIP_ESP32H2、CHIP_ESP32P4 枚举
- chip.c: 实现 InitEsp32C5、InitEsp32C61、InitEsp32H2、InitEsp32P4 初始化函数
- chip.c: 补充 eFuse 布局、MAC 地址偏移、SPI 寄存器偏移
- chip.c: 实现 Chip_GetBootMessage 启动日志
- main.c: Device Properties 对话框添加新芯片选项
- REQUIREMENTS.md、DEVELOPMENT.md: 同步更新芯片支持列表

---

## 远期规划 - 新芯片支持

支持 esptool 官方新增的 ESP 芯片（部分特性待完善）。

| 芯片 | 特性 | eFuse基址 | SPI基址 | 晶振 |
|------|------|-----------|---------|------|
| ESP32-H21 | BT5+802.15.4, RISC-V单核96MHz | 0x600B4000 | 0x60002000 | 32MHz |
| ESP32-H4 | BT5+802.15.4, RISC-V双核96MHz | 0x600B1800 | 0x60099000 | 32MHz |
| ESP32-E22 | WiFi 6E+BT5.4, 双核500MHz | 0xC4008000 | 0xC3003000 | 动态检测 |
| ESP32-S31 | WiFi 6+BT5.4+802.15.4, 双核+LP核300MHz | 0x20715000 | 0x20501000 | 40MHz |

**实现内容：**
- chip.h: 添加 CHIP_ESP32H21、CHIP_ESP32H4、CHIP_ESP32E22、CHIP_ESP32S31 枚举
- chip.c: 实现各芯片初始化函数
- chip.c: 补充 eFuse 布局、MAC 地址偏移、SPI 寄存器偏移
- chip.c: 实现 Chip_GetBootMessage 启动日志
- main.c: Device Properties 对话框添加新芯片选项
- REQUIREMENTS.md、DEVELOPMENT.md: 同步更新芯片支持列表

**注意事项：**
- ESP32-E22 的 eFuse 字段尚未完全分配，部分功能需参考 esptool 最新实现
- ESP32-S31 的 eFuse 布局与其他芯片不同（BLOCK1 偏移 0x050）
- ESP32-H4 支持 EUI64 MAC 格式

---

## 低优先级 - eFuse 状态显示扩展

在状态栏或对话框中显示更多 eFuse 状态信息，待评估是否需要实现。

### JTAG 调试接口状态

| 标志位 | 说明 | 适用芯片 |
|--------|------|----------|
| `DIS_PAD_JTAG` | 禁用 PAD JTAG（永久） | ESP32-S2/S3/C2/C3/C5/C6 |
| `DIS_USB_JTAG` | 禁用 USB JTAG | ESP32-S3/C3/C5/C6 |
| `SOFT_DIS_JTAG` | 软件禁用 JTAG（可恢复） | ESP32-S2/S3/C3/C5/C6 |
| `JTAG_DISABLE` | 禁用 JTAG | ESP32 |

### Secure Boot 安全启动状态

| 标志位 | 说明 | 适用芯片 |
|--------|------|----------|
| `SECURE_BOOT_EN` | 启用安全启动 | 所有（除 ESP8266） |
| `SECURE_BOOT_KEY_REVOKE0/1/2` | 撤销安全启动密钥 | ESP32-S2/S3/C3/C5/C6 |
| `SECURE_BOOT_AGGRESSIVE_REVOKE` | 激进撤销模式 | ESP32-S2/S3/C3/C5/C6 |

### 温度信息

| 标志位 | 说明 | 适用芯片 |
|--------|------|----------|
| `TEMP` | 芯片工作温度范围 | ESP32-C5/P4/S31 |
| `TEMP_CALIB` | 温度传感器校准数据 | ESP32-S2/S3/C2/C3 |
| `FLASH_TEMP` | Flash 温度等级 | ESP32-S3/C2/C3 |
| `PSRAM_TEMP` | PSRAM 温度等级 | ESP32-S3 |

### 蓝牙状态（仅 ESP32）

| 标志位 | 说明 |
|--------|------|
| `DISABLE_BT` | 禁用蓝牙 |

---
