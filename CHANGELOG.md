# FakeEsptool - 变更记录

本文件记录 FakeEsptool 的版本变更历史。

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
