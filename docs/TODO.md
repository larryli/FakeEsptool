# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 远期规划 - 新芯片支持

支持 esptool 官方新增的 ESP 芯片（部分特性待完善）。

| 芯片 | 特性 | eFuse基址 | SPI基址 | 晶振 | IMAGE_CHIP_ID | 继承 |
|------|------|-----------|---------|------|---------------|------|
| ESP32-H21 | BT5+802.15.4, RISC-V单核96MHz | 0x600B4000 | 0x60003000 | 32MHz | 25 | ESP32H2ROM |
| ESP32-H4 | BT5+802.15.4, RISC-V双核96MHz | 0x600B1800 | 0x60099000 | 32MHz | 28 | ESP32C3ROM |
| ESP32-E22 | WiFi 6E+BT5.4, 双核500MHz | 0xC4008000 | 0xC3003000 | 动态检测 | 31 | ESP32ROM |

**实现内容：**
- chip.h: 添加芯片枚举
- chip.c: 实现各芯片初始化函数
- chip.c: 补充 eFuse 布局、MAC 地址偏移、SPI 寄存器偏移
- chip.c: 实现 fesp_chip_get_boot_message 启动日志
- main.c: Device Properties 对话框添加新芯片选项
- REQUIREMENTS.md、DEVELOPMENT.md: 同步更新芯片支持列表

**注意事项：**
- ESP32-E22 的 eFuse 字段尚未完全分配（DIS_DOWNLOAD_MANUAL_ENCRYPT、SPI_BOOT_CRYPT_CNT 未定义）
- ESP32-E22 的 UART_DATE_REG 地址不同于其他芯片（0xC310208C）
- ESP32-E22 的 GPIO_STRAP_REG 在 0xC310D000
- ESP32-H21 无 stub loader
- ESP32-E22 无 stub loader
- ESP32-H4 支持 EUI64 MAC 格式

---
