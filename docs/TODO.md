# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 高优先级 - Bug 修复

### 修复 FLASH_DEFL_DATA 分包解压 Bug（积累解压方案）

**问题描述：**

esptool 客户端（esptool-js、Python esptool）在压缩烧录时，将整个镜像用 zlib 压缩为一个连续的 deflate 流，然后按 `FLASH_WRITE_SIZE`（通常 0x400 = 1024 字节）切分为多个 `FLASH_DEFL_DATA` 包发送。当前实现（`esptool.c:HandleFlashDeflData`）将每个包当作独立的 deflate 流解压，导致第二个及后续包解压失败。

**根本原因：**

`deflate.c:deflate_decompress()` 不支持流式输入。当 deflate block 边界跨越两个包时，第一个包解压到 block 中间会因输入耗尽而失败（`deflate_read_bits()` 返回 -1）。此外，`deflate_decode_dynamic()` 构建 Huffman 表的中间状态无法跨包保持。

**修复方案：积累解压**

不修改 `deflate.c`，改为在 `esptool.c` 中积累所有 `FLASH_DEFL_DATA` 包的压缩数据，到 `FLASH_DEFL_END` 时一次性调用 `deflate_decompress()` 解压。

**Stub 模式下此方案可行的原因：**
- Stub 模式下 `FLASH_DEFL_DATA` 的响应只表示"数据已收到"，不保证写入 flash
- 客户端收到响应后继续发送下一个包
- 在 `FLASH_DEFL_END` 时解压并写入 flash，符合 Stub 模式的行为预期

**⚠️ 多文件烧录时的生命周期管理**

三个烧录器在多文件烧录时对 `FLASH_DEFL_END` 的处理不同：

| 实现 | Stub 模式 | ROM 模式 |
|------|----------|----------|
| esptool-js | 每个文件后发 `FLASH_DEFL_END` | 文件间**不发** `FLASH_DEFL_END` |
| web-esptool | 全部文件完成后发一次 | **不发** `FLASH_DEFL_END` |
| Python esptool | 最后一个 cycle 完成后发一次 | **不发** `FLASH_DEFL_END` |

这意味着 ROM 模式下，新的 `FLASH_DEFL_BEGIN` 可能在上一个文件的积累数据**未处理**的情况下到达。

**需要处理积累缓冲区的所有场景：**

| 场景 | 触发方式 | 实现要求 |
|------|---------|---------|
| `FLASH_DEFL_END` | 正常结束压缩写入 | 解压积累数据 → 写入 flash → 释放缓冲区 |
| `FLASH_DEFL_BEGIN`（重复） | ROM 模式多文件烧录不发 END | 解压积累数据 → 写入 flash → 释放缓冲区 → 开始新积累 |
| `FLASH_BEGIN` | 客户端从压缩切换到非压缩模式 | **保留缓冲区**，等待后续 `FLASH_DEFL_END` 处理 |
| `FLASH_END` | 非压缩写入结束 | 释放积累缓冲区（不写入） |
| `ERASE_FLASH` / `ERASE_REGION` | 擦除操作可能中断烧录 | 释放积累缓冲区（不写入） |
| `RUN_USER_CODE` | 软复位 | 释放积累缓冲区（不写入） |
| 硬件复位 | DTR/RTS 信号 → `Esptool_ResetState` | 安全释放积累缓冲区 |

**实现时必须注意：**
- **内存分配**：使用 `uncompressed_size` 大小预分配积累缓冲区（压缩后通常小 30-50%，最大固件几 MB 可承受）
- **错误处理**：解压失败时返回 `ESP_FAIL` 中止整个烧录（因为最后有 MD5 校验，跳过失败文件无意义）
- **时序约束**：`FLASH_DEFL_END` 必须同步完成解压和写入，不能异步（Stub 模式下客户端随后可能发送 MD5 验证）
- **超时风险**：`FLASH_DEFL_BEGIN` 时处理上一个文件的积累数据可能耗时较长，详见 `DEVELOPMENT.md`
- `HandleFlashDeflBegin` 中，先检查是否有未处理的积累缓冲区，若有则解压并写入 flash（使用 `ctx->defl_offset` 和 `ctx->defl_unc_size`），然后释放缓冲区
- `HandleFlashDeflData` 中，将压缩数据追加到积累缓冲区，立即返回 ESP_OK
- `HandleFlashDeflEnd` 中，解压积累数据并写入 flash，释放缓冲区
- `HandleFlashBegin` 中**不释放**缓冲区（客户端可能在 `FLASH_DEFL_DATA` 后发送 `FLASH_BEGIN`，再发送 `FLASH_DEFL_END`）
- `HandleFlashEnd` 中释放积累缓冲区（不写入）
- `HandleEraseFlash` 和 `HandleEraseBlock` 中释放积累缓冲区
- `Esptool_ResetState` 中安全释放积累缓冲区
- 不要假设 `FLASH_DEFL_END` 一定会在下一个 `FLASH_DEFL_BEGIN` 之前到达

**实现内容：**
- esptool.h: ESPTOOL_CTX 添加 `defl_buf`、`defl_buf_size`、`defl_buf_cap`、`defl_offset`、`defl_unc_size`
- esptool.c: 添加 `Defl_FreeBuffer()` 和 `Defl_FlushBuffer()` 辅助函数
- esptool.c: HandleFlashDeflBegin 检查并处理上一次积累数据，分配新缓冲区
- esptool.c: HandleFlashDeflData 追加数据到缓冲区
- esptool.c: HandleFlashDeflEnd 解压并写入 flash，释放缓冲区
- esptool.c: HandleFlashEnd/HandleEraseFlash/HandleEraseBlock 释放积累缓冲区
- esptool.c: HandleFlashBegin **不释放**缓冲区（等待后续 FLASH_DEFL_END）
- esptool.c: Esptool_ResetState 安全释放积累缓冲区并重置所有 deflate 字段
- tests: 添加分包解压测试用例

**参考：**
- esptool-js: `esploader.ts:1554-1604` — `deflate()` 压缩整块 → 按 `FLASH_WRITE_SIZE` 切分 → 逐块发送
- Python esptool: `cmds.py:1392-1464` — `zlib.compress()` 压缩整块 → 按 `FLASH_WRITE_SIZE` 切分

---

## 中优先级 - 流式解压支持

### 集成 miniz 库替换 deflate.c（流式解压方案）

**问题描述：**

积累解压方案（高优先级）虽然可行，但存在以下局限：
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
- 生命周期管理与积累解压方案相同（见高优先级待办）
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
