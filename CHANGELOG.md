# FakeEsptool - 变更记录

本文件记录 FakeEsptool 的版本变更历史。

---

## [2026.7.14.0] - 2026-07-14

### 安全修复

- **协议层防御性校验加固**：修复 11 处整数溢出、静默成功、空指针解引用等安全问题
  - `handle_read_flash`：拒绝 `bsize=0`（死循环）和超大 `bsize`（`bsize*2+2` 整数溢出导致 SLIP 编码越界写入）
  - `handle_read_flash`：ACK 移到内存分配成功后发送，避免 OOM 时客户端挂起
  - `handle_read_flash`：循环前校验 `addr+offset` 不溢出
  - `handle_flash_defl_data`：`defl_buf_size+data_len` 溢出前检查，防止 memcpy 堆越界写入
  - `handle_flash_defl_data`：`data_len` 与 `pkt->size` 不匹配时返回 FESP_FAIL（非静默成功）
  - `handle_flash_data`：`flash_offset` 累加前溢出检查
  - `handle_flash_data`：`data_len` 与 `pkt->size` 不匹配时返回 FESP_FAIL
  - `handle_flash_end`：`defl_flush_buffer` 失败时返回 FESP_FAIL（非静默成功）
  - `fesp_flash_erase`：`addr+len` 整数溢出改为 `len > ctx->size - addr` 判断
  - `fesp_process_frame`：添加 `ctx`/`chip`/`flash` NULL 守卫
  - `CHECK_PKT_SIZE` 宏：包过小时发送 FESP_FAIL 响应（非静默丢弃）
  - `fesp_send_response_ex`：SLIP 编码失败时记录日志
- **Pelles C 兼容**：移除 `esptool.c` 中 `#pragma warn(disable : 2802)`，用代码可证明的边界检查替代

### 文档

- PROTOCOL.md 附录 A：芯片列表从 7 款扩充为 12 款（新增 C5/C61/H2/P4/S31）
- PROTOCOL.md 3.22 节：IMAGE_CHIP_ID 表和 GET_SECURITY_INFO 行为表同步扩充
- esptool.c 注释：`handle_get_security_info` 芯片列表同步更新

---

## [2026.7.3.0] - 2026-07-03

### 功能增强

- **eFuse 块数据导入导出（QEMU 兼容）**：
  - 新增 Storage 菜单 Import eFuse / Export eFuse 功能
  - 导出格式与 QEMU/esp-emulator 块数据格式兼容
  - 支持所有芯片（ESP8266 菜单禁用）
  - 工具栏导入/导出按钮改为下拉菜单，包含 Flash 和 eFuse 选项
- **eFuse 易失数据清除**：
  - 串口连接和设备收到 RESET 信号时，自动清除 eFuse 易失性区域（PGM 寄存器、控制寄存器）
  - 2 线连接（无 DTR/RTS）下通过断开重连触发清除
- **.esp 设备文件格式升级 v2**：
  - 新芯片保存 v2 格式：eFuse 仅包含块读数据（QEMU 兼容）
  - ESP8266 保持 v1 格式
  - v1 文件加载自动兼容，提取块数据
- **文件对话框标题**：
  - 所有 Open/Save 对话框添加中英文标题
- **工具栏下拉菜单**：
  - Import/Export 按钮改为下拉样式，支持 Flash 和 eFuse 选项
  - ESP8266 时 eFuse 选项禁用

### 工具

- **efuse_convert.py**：
  - 新增 QEMU 块数据格式 ↔ espefuse --virt 格式相互转换脚本
  - 支持全部 11 种芯片

### 编码改进

- 启用 C11 安全字符串函数（`__STDC_WANT_LIB_EXT1__`）
- 全部 `wcscpy`/`wcscat`/`swprintf` 替换为 `_s` 安全版本

### 文档

- REQUIREMENTS.md：新增 eFuse 导入导出菜单说明、设备文件格式 v2 描述、工具栏下拉菜单说明
- DEVELOPMENT.md：新增 .esp v2 格式说明、eFuse 导入导出实现、espefuse --virt 格式对比、LoadStr 使用规范
- README.md：2 线连接说明补充 PGM 限制和 RESET 信号说明

## [2026.6.29.0] - 2026-06-29

### 功能增强

- **新增芯片支持（5 款）**：
  - ESP32-C5：WiFi 6 双频+BT5+802.15.4，RISC-V 单核+LP核 240MHz，支持 40/48MHz 晶振
  - ESP32-C61：WiFi 6+BT5，RISC-V 单核 160MHz
  - ESP32-H2：BT5+802.15.4，RISC-V 单核 96MHz，固定 32MHz 晶振
  - ESP32-P4：高性能 MCU，双核+LP核 400MHz，无无线
  - ESP32-S31：WiFi 6+BT5.4+802.15.4，双核+LP核 300MHz
- **晶振频率下拉菜单**：动态填充选项，根据芯片类型显示可用频率

### 重构

- **esptool 协议层重构**：将 `src/esptool/` 迁移至 `src/fesptool/`，新增 HAL 抽象层（`fesptool_hal.c/h`）
- **设备文件分离**：新增 `device_file.c/h`，将设备文件 I/O 逻辑从 main.c 独立
- **协议信号处理**：新增 `app_protocol.c/h`，重构串口信号状态机逻辑
- **函数命名统一**：解压缩、初始化等函数统一为大写开头命名
- **加密/解压重构**：更新 HAL 接口支持新的上下文结构
- **日志机制重构**：使用 `FESP_HAL_LOG_HAS_DEBUG` 宏控制调试日志输出
- **内存管理封装**：新增 `mem.c/h`，封装 Windows Heap API，支持内存泄漏追踪
- **MD5 实现**：新增 `md5.c/h`，跨平台 MD5 哈希实现

### 单元测试

- 新增 SLIP 协议编码器/解码器测试
- 新增 MD5 哈希测试（RFC 1321 测试向量）
- 新增 Flash 存储模拟测试（初始化、读写、擦除、边界条件）
- 新增 ESP 芯片模拟测试（初始化、关闭、属性访问、MAC 地址）
- 新增 eFuse 模拟测试（默认状态、加密计数、密钥用途、下载模式）
- 新增 esptool 协议处理程序测试（初始化/关闭、校验和、SLIP 帧、基本命令）

### 修复

- 修复 ESP32 SPI Flash ID 检测失败（eFuse 控制器地址映射错误拦截 SPI 寄存器写入）
- 修复 ESP32-C3/C6/S3 的 `write_chip_id_to_efuse` 调用（这些芯片使用 SECURITY_INFO 检测，无需写入 eFuse 0x4C）
- 修复 ESP32-S31 eFuse BLOCK1 偏移（0x050 而非 0x044）
- 修正所有芯片启动日志为官方文档/实际硬件捕获值
- 修复 `Esptool_ProcessFrame` 中的字符类型，使用标准字符类型替代 WCHAR
- 修复 `Flash_Init` 中 HeapAlloc 参数

### 文档

- 更新 README.md 芯片支持列表（12 款芯片）
- 更新 REQUIREMENTS.md 芯片支持列表、设备属性描述、晶振频率说明
- 更新 TODO.md 移除已完成项，修正 ESP32-H21 SPI 基址，将 ESP32-S31 提升为高优先级
- 更新 DEVELOPMENT.md 架构概览、HAL 接口说明、测试用例说明
- 更新 API.md 文档以反映新文件和接口变更

---

## [2026.6.25.0] - 2026-06-25

### 功能增强

- **密钥管理**：
  - 更新 KEY5 处理逻辑，支持 ESP32-S3/C3/C6 芯片，修复硬件缺陷导致的 XTS_AES 目的拒绝问题
- **用户界面**：
  - 添加新设备、打开设备、保存设备的工具提示文本

### 修复

- 修复 Flash_Init 中 HeapAlloc 的参数，确保正确分配内存
- 将"生产模式"更改为"量产模式"，更新相关文档和代码注释

### 文档

- 添加量产安全 eFuse 配置和相关测试步骤
- 更新加密烧录指南，优化文档结构和内容
- 添加 Mimo Code AI 编程助手的贡献说明

---

## [2026.6.17.0] - 2026-06-17

### 功能增强

- **状态栏优化**：
  - 布局从 7 栏精简为 6 栏（芯片+Flash、加密、下载模式、安全启动、JTAG、端口+配置）
  - 新增安全启动状态显示（Secure Boot）
  - 新增 JTAG 状态显示（三态：可用/部分可用/全部禁用）
  - Tooltip 显示 eFuse 原始值（每个栏位悬停显示对应 eFuse 字段名和值）
  - 芯片栏 Tooltip 显示 XTAL 频率和 MAC 地址
  - 端口栏合并串口配置显示
- **密钥管理增强**：
  - Purpose 列显示真实 KEY_PURPOSE eFuse 值（不再硬编码）
  - 新增 Purpose 按钮，可修改密钥块用途（S2/S3/C3/C6 支持）
  - ESP32-S3 KEY5 禁用 XTS_AES 选项（硬件 bug）
  - ESP32/C2 Purpose 按钮禁用（固定用途）
- **加密烧录支持**：
  - 修复 ESP32 eFuse 寄存器地址映射（EFUSE_BASE 和 EFUSE_RD_REG_BASE）
  - 修复 ESP32 BLOCK0 默认值初始化（CHIP_CPU_FREQ_RATED、FLASH_CRYPT_CONFIG）
  - 修复 KEY_PURPOSE eFuse 偏移（按芯片区分 BLOCK0 基址）
  - 所有芯片（除 ESP8266）加密烧录+验证通过
- **菜单增强**：
  - ESP8266 时 Encryption State 和 Download Mode 菜单禁用
  - ESP32 时 Download Secure 菜单项禁用
  - Encryption State / Download Mode 菜单可实际修改 eFuse 状态

### 协议修复

- **GET_SECURITY_INFO**：ESP8266/ESP32 返回正确的 2 字节失败状态（`FF 00`），而非 `ROM_INVALID_RECV_MSG` 错误包
- **ESP8266 MAC 读取**：修复 eFuse 基址（`0x3FF00000`），MAC 寄存器读取正确
- **ESP32 eFuse 控制器**：支持 EFUSE_BASE（`0x3FF42000`）范围的写入操作
- **加密密钥选择**：`Chip_GetEncryptionKeyOffset` 按 KEY_PURPOSE 扫描密钥块（S2/S3/C3/C6）

### 文档

- 新增设计原则章节（协议层/GUI 层分离）
- 更新状态栏文档（6 栏布局、Tooltip、三态 JTAG）
- 新增 KEY_PURPOSE eFuse 字段说明
- PROTOCOL.md 新增 eFuse 控制器模拟附录
- DEVELOPMENT.md 同步新增函数 API
- Trace 宏使用规范化（esptool 目录用 TRACE_PROTO，其他用 TRACE_FW）

---

## [2026.6.14.0] - 2026-06-14

### 首次发布

ESP 芯片设备端模拟器的第一个正式版本。

### 功能

- **芯片支持**：ESP8266、ESP32、ESP32-S2、ESP32-S3、ESP32-C2、ESP32-C3、ESP32-C6
- **协议支持**：esptool SLIP 协议基本实现（NAND 命令除外）
  - 同步握手（SYNC）
  - 芯片检测（READ_REG / GET_SECURITY_INFO）
  - Flash 烧录（FLASH_BEGIN / FLASH_DATA / FLASH_END）
  - 压缩烧录（FLASH_DEFL_BEGIN / FLASH_DEFL_DATA / FLASH_DEFL_END）
  - Flash 读取（READ_FLASH）
  - Flash 擦除（ERASE_FLASH / ERASE_REGION）
  - 内存写入（MEM_BEGIN / MEM_DATA / MEM_END）
  - 寄存器读写（READ_REG / WRITE_REG）
  - SPI 参数设置（SPI_SET_PARAMS / SPI_ATTACH）
  - 波特率切换（CHANGE_BAUDRATE）
  - MD5 验证（SPI_FLASH_MD5）
  - 安全信息（GET_SECURITY_INFO）
  - *未实现：NAND Flash 命令（0xD5-0xDE）*
- **设备管理**：
  - 新建、打开、保存 .esp 设备文件
  - 设备属性修改（芯片类型、晶振频率、Flash 大小、MAC 地址）
  - Flash 导入/导出（.bin 文件）
  - 设备内容导出到文本文件
- **串口通信**：
  - 虚拟串口和实体串口支持
  - DTR/RTS 信号控制（下载模式检测）
  - 动态波特率切换
  - 启动日志输出
- **用户界面**：
  - 多语言支持（英文、简体中文）
  - 实时 HEX 日志显示（带时间戳和颜色）
  - 工具栏快捷操作
  - 状态栏信息显示
  - 配置持久化（字体、端口、设备文件）
- **文件操作**：
  - 命令行打开文件（支持相对路径）
  - 拖放文件到窗口打开
  - .esp 文件关联
  - 单实例模式

### 技术特性

- C + CMake + Win32 API
- 静态链接 VC++ 运行时（无需安装 VC++ Redistributable）
- DEFLATE 压缩数据自动解压
- SLIP 协议编解码
- 后台线程文件写入（Dump Device As）

---

## [未发布]

### 计划功能

详见 [TODO.md](docs/TODO.md)。
