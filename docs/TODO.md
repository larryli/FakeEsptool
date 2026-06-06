# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 中优先级 - 流式解压支持

### 集成 miniz 库替换 deflate.c（流式解压方案）

**问题描述：**

当前积累解压方案虽然可行，但存在以下局限：
- 需要为每个文件分配 `uncompressed_size` 大小的内存来存储压缩数据
- 无法在 `FLASH_DEFL_DATA` 时提供真实的解压状态反馈
- 大文件烧录时内存占用较高

**修复方案：**

集成第三方 miniz 库（单文件 zlib 兼容实现）替换自定义 `deflate.c`，使用其流式 `mz_inflate()` API 支持分块喂数据。

- 删除 `src/utils/deflate.c`、`src/utils/deflate.h`
- 删除 `tests/test_deflate.c`、`tests/test_data.h`、`tests/generate_test_data.py`
- 集成 miniz 到 `lib/miniz/`（单头文件库，MIT 许可证）
- `FLASH_DEFL_BEGIN` 时调用 `mz_inflateInit2()` 初始化流式解压器
- `FLASH_DEFL_DATA` 时设置 `next_in`/`avail_in` 和 `next_out`/`avail_out`，调用 `mz_inflate(MZ_NO_FLUSH)` 流式解压，立即写入 flash
- `FLASH_DEFL_END` 时调用 `mz_inflateEnd()` 释放资源

**实现时必须注意：**
- 生命周期管理与积累解压方案相同（见 `DEVELOPMENT.md` 积累解压方案实现细节）
- 输出缓冲区建议使用固定大小（如 `FLASH_WRITE_SIZE`），循环调用 `mz_inflate`，缓冲区满即写入 flash
- 解压失败时（`MZ_DATA_ERROR`/`MZ_MEM_ERROR`）返回 `ESP_FAIL` 给客户端，清理资源

**实现内容：**
- lib/miniz/: 集成 miniz 库（miniz.h 单头文件）
- CMakeLists.txt: 添加 miniz 编译，移除 deflate.c
- esptool.h: ESPTOOL_CTX 添加 `mz_stream`、解压输出缓冲区（建议 `FLASH_WRITE_SIZE` 大小）、解压器活跃标志
- esptool.c: HandleFlashDeflBegin 初始化 `mz_inflateInit2()`，分配输出缓冲区
- esptool.c: HandleFlashDeflData 循环调用 `mz_inflate(MZ_NO_FLUSH)`，输出缓冲区满时写入 flash
- esptool.c: HandleFlashDeflEnd 调用 `mz_inflateEnd()`，释放输出缓冲区
- LICENSE: 追加 miniz 版权声明（MIT 许可证，Copyright (c) 2013 Rich Geldreich）
- about.c: 添加「第三方库」静态文本控件，显示 miniz 致谢信息
- resource.rc: 对话框模板中添加致谢控件
- 删除: src/utils/deflate.c、src/utils/deflate.h
- 删除: tests/test_deflate.c、tests/test_data.h、tests/generate_test_data.py
- tests: 基于 miniz 添加分包解压测试用例

**参考：**
- miniz: https://github.com/richgel999/miniz

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
- main.c: New Device/Device Properties 对话框添加新芯片选项
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
- main.c: New Device/Device Properties 对话框添加新芯片选项
- REQUIREMENTS.md、DEVELOPMENT.md: 同步更新芯片支持列表

**注意事项：**
- ESP32-E22 的 eFuse 字段尚未完全分配，部分功能需参考 esptool 最新实现
- ESP32-S31 的 eFuse 布局与其他芯片不同（BLOCK1 偏移 0x050）
- ESP32-H4 支持 EUI64 MAC 格式
