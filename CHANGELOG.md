# FakeEsptool - 变更记录

本文件记录 FakeEsptool 的版本变更历史。

---

## [2026.6.14.0] - 2026-06-14

### 首次发布

ESP 芯片设备端模拟器的第一个正式版本。

### 功能

- **芯片支持**：ESP8266、ESP32、ESP32-S2、ESP32-S3、ESP32-C2、ESP32-C3、ESP32-C6
- **协议支持**：esptool SLIP 协议完整实现
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
