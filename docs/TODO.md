# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 高优先级 - fesptool 模拟引擎单元测试

为 `src/fesptool/` 模拟引擎添加完整单元测试。需要提供 mock `fesptool_hal` 实现（捕获 Write、记录 Log、模拟 MD5/Deflate/Encrypt 使用当前平台实现）。

**测试优先级与模块：**

| 优先级 | 模块 | 测试要点 |
|--------|------|----------|
| 1 | `flash.c` | read/write（AND 语义）、erase（0xFF 恢复）、calc_md5、边界条件 |
| 2 | `slip.c` | 编码/解码往返、转义序列、帧边界、缓冲区溢出保护 |
| 3 | `efuse.c` | 字段偏移正确性、ReadEfuseBits/WriteEfuseBits/ClearEfuseBits、key purpose、加密状态查询 |
| 4 | `chip.c` | 各芯片 Init、ReadReg/WriteReg 映射、SPI 寄存器、eFuse 控制器烧录、启动消息 |
| 5 | `esptool.c` | SYNC 握手、READ_REG 检测、FLASH_BEGIN/DATA/END 完整流程、压缩烧录、加密烧录、GET_SECURITY_INFO、命令状态机 |

**Mock HAL 要求：**
- `fesp_hal_write`: 捕获写入数据到缓冲区，供断言验证 SLIP 响应
- `fesp_hal_log_i/log_e`: 记录日志消息，供断言验证
- `fesp_hal_set_baud_rate` / `fesp_hal_modified`: 记录调用次数
- MD5/Deflate/Encrypt: 使用当前平台实现（Windows CryptoAPI、zlib、AES-XTS）

**测试框架：** 复用现有 `tests/` 目录结构，CMake + ctest。

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
