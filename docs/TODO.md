# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 高优先级 - Bug 修复

### 修复 FLASH_DEFL_DATA 分包解压 Bug

**问题描述：**

esptool 客户端（esptool-js、Python esptool）在压缩烧录时，将整个镜像用 zlib 压缩为一个连续的 deflate 流，然后按 `FLASH_WRITE_SIZE`（通常 0x400 = 1024 字节）切分为多个 `FLASH_DEFL_DATA` 包发送。当前实现（`esptool.c:HandleFlashDeflData`）将每个包当作独立的 deflate 流解压，导致第二个及后续包解压失败。

**根本原因：**

`deflate.c:deflate_decompress()` 不支持流式输入。当 deflate block 边界跨越两个包时，第一个包解压到 block 中间会因输入耗尽而失败（`deflate_read_bits()` 返回 -1）。此外，`deflate_decode_dynamic()` 构建 Huffman 表的中间状态无法跨包保持。

**修复方案：**

集成第三方 miniz 库（单文件 zlib 兼容实现）替换自定义 `deflate.c`，使用其流式 `mz_inflate()` API 支持分块喂数据。

- 删除 `src/utils/deflate.c`、`src/utils/deflate.h`
- 删除 `tests/test_deflate.c`、`tests/test_data.h`、`tests/generate_test_data.py`
- 集成 miniz 到 `lib/miniz/`（单头文件库，MIT 许可证）
- `FLASH_DEFL_BEGIN` 时调用 `mz_inflateInit2()` 初始化流式解压器
- `FLASH_DEFL_DATA` 时设置 `next_in`/`avail_in` 和 `next_out`/`avail_out`，调用 `mz_inflate(MZ_NO_FLUSH)` 流式解压，立即写入 flash
- `FLASH_DEFL_END` 时调用 `mz_inflateEnd()` 释放资源

**⚠️ 注意：多文件烧录时解压器生命周期差异**

三个烧录器在多文件烧录时对 `FLASH_DEFL_END` 的处理不同：

| 实现 | Stub 模式 | ROM 模式 |
|------|----------|----------|
| esptool-js | 每个文件后发 `FLASH_DEFL_END` | 文件间**不发** `FLASH_DEFL_END` |
| web-esptool | 全部文件完成后发一次 | **不发** `FLASH_DEFL_END` |
| Python esptool | 最后一个 cycle 完成后发一次 | **不发** `FLASH_DEFL_END` |

这意味着 ROM 模式下，新的 `FLASH_DEFL_BEGIN` 可能在上一个文件的解压器**未释放**的情况下到达。

**需要清理 miniz 资源的所有场景：**

| 场景 | 触发方式 | 实现要求 |
|------|---------|---------|
| `FLASH_DEFL_END` | 正常结束压缩写入 | 在 `HandleFlashDeflEnd` 中调用 `mz_inflateEnd()` |
| `FLASH_DEFL_BEGIN`（重复） | ROM 模式多文件烧录不发 END | 在 `HandleFlashDeflBegin` 中先检查并释放已有解压器 |
| 硬件复位 | DTR/RTS 信号 → `Esptool_ResetState` | 在 `Esptool_ResetState` 中安全清理 |
| 软件复位 | `RUN_USER_CODE` → `Esptool_ResetState` | 同上 |
| `FLASH_END` | 客户端从压缩切换到非压缩模式 | 在 `HandleFlashEnd` 中检查并释放 |
| `FLASH_BEGIN` | 客户端重新开始烧录 | 在 `HandleFlashBegin` 中检查并释放 |

**实现时必须注意：**
- `HandleFlashDeflBegin` 中，在调用 `mz_inflateInit2()` 之前，必须检查是否已有活跃的 `mz_stream`，若有则先调用 `mz_inflateEnd()` 释放
- `HandleFlashEnd` 和 `HandleFlashBegin` 中也要检查并释放（处理模式切换场景）
- `Esptool_ResetState` 中安全清理解压器资源（处理复位场景）
- 不要假设 `FLASH_DEFL_END` 一定会在下一个 `FLASH_DEFL_BEGIN` 之前到达
- 输出缓冲区建议使用固定大小（如 `FLASH_WRITE_SIZE`），循环调用 `mz_inflate`，缓冲区满即写入 flash，避免为大文件分配 `uncompressed_size` 内存
- 解压失败时返回 `ESP_FAIL` 给客户端，清理资源，不要继续处理后续 DATA 包

**实现内容：**
- lib/miniz/: 集成 miniz 库（miniz.h 单头文件）
- CMakeLists.txt: 添加 miniz 编译，移除 deflate.c
- esptool.h: ESPTOOL_CTX 添加 `mz_stream`、解压输出缓冲区（建议 `FLASH_WRITE_SIZE` 大小）、解压器活跃标志
- esptool.c: HandleFlashDeflBegin 初始化 `mz_inflateInit2()`，分配输出缓冲区
- esptool.c: HandleFlashDeflData 循环调用 `mz_inflate(MZ_NO_FLUSH)`，输出缓冲区满时写入 flash
- esptool.c: HandleFlashDeflEnd 调用 `mz_inflateEnd()`，释放输出缓冲区
- esptool.c: HandleFlashEnd/HandleFlashBegin 检查并释放已有解压器
- esptool.c: Esptool_ResetState 安全清理解压器资源
- esptool.c: 解压失败时（`MZ_DATA_ERROR`/`MZ_MEM_ERROR`）返回错误状态码，清理资源，中止写入
- 删除: src/utils/deflate.c、src/utils/deflate.h
- 删除: tests/test_deflate.c、tests/test_data.h、tests/generate_test_data.py
- tests: 基于 miniz 添加分包解压测试用例

**参考：**
- esptool-js: `esploader.ts:1554-1604` — `deflate()` 压缩整块 → 按 `FLASH_WRITE_SIZE` 切分 → 逐块发送
- Python esptool: `cmds.py:1392-1464` — `zlib.compress()` 压缩整块 → 按 `FLASH_WRITE_SIZE` 切分

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
