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

## 中优先级 - 加密烧录支持

支持 esptool.py 的加密烧录功能（`--encrypt` 参数）。

### 功能概述

加密烧录是 ESP 芯片的安全功能，客户端发送明文数据，设备端使用 eFuse 中的密钥加密后写入 Flash。

**各芯片加密烧录支持情况**：

| 芯片 | ROM 模式 | Stub 模式 | 说明 |
|------|---------|----------|------|
| ESP8266 | ❌ 不支持 | ❌ 不支持 | 芯片不支持 Flash 加密 |
| ESP32 | ❌ 不支持 | ✅ 支持 | ROM 不支持扩展参数格式 |
| ESP32-S2 | ✅ 支持 | ✅ 支持 | |
| ESP32-S3 | ✅ 支持 | ✅ 支持 | |
| ESP32-C2 | ✅ 支持 | ✅ 支持 | |
| ESP32-C3 | ✅ 支持 | ✅ 支持 | |
| ESP32-C6 | ✅ 支持 | ✅ 支持 | |

### 协议说明

加密烧录复用普通烧录命令，通过 `encrypted` 标志位区分：

| 命令 | 请求格式 | encrypted 字段位置 |
|------|----------|-------------------|
| FLASH_BEGIN (0x02) | 20 字节（ROM ESP32/ESP8266 为 16 字节） | data[16..19] |
| FLASH_DEFL_BEGIN (0x10) | 20 字节（ROM ESP32/ESP8266 为 16 字节） | data[16..19] |

**encrypted 字段发送条件**（来自 esptool.py `loader.py:1089`）：
```python
if self.IS_STUB or self.CHIP_NAME not in ("ESP32", "ESP8266"):
    params += struct.pack("<I", 1 if encrypted_write else 0)
```
- Stub 模式：所有芯片都发送（20 字节）
- ROM 模式 ESP32/ESP8266：不发送（16 字节），ROM 不支持扩展参数格式
- ROM 模式其他芯片：发送（20 字节）

**关键点**：`encrypted=1` 告诉设备"请在写入前加密数据"，客户端发送的是明文数据。

### 密钥管理

完整加密烧录工作流需要三个工具配合：

```
1. espsecure generate-flash-encryption-key -k 256 key.bin              # 生成密钥（离线，不与设备通信）
2. espefuse --chip esp32c3 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY # 烧录密钥到 eFuse（通过 READ_REG/WRITE_REG）
3. esptool --encrypt write-flash 0x0 firmware.bin                      # 加密烧录（设备端加密后写入 Flash）
```

| 工具 | 功能 | 与设备通信 | FakeEsptool 支持状态 |
|------|------|-----------|-------------------|
| espsecure | 密钥生成、数据加密/解密 | ❌ 离线工具 | 不需要支持 |
| espefuse | eFuse 读写、密钥烧录 | ✅ READ_REG/WRITE_REG | ✅ 已支持 |
| esptool | Flash 烧录 | ✅ SLIP 协议 | ⚠️ 需要添加 encrypted 支持 |

### 密钥存储

加密密钥存储在芯片的 eFuse（一次性可编程存储器）中：

| 芯片 | eFuse 块数量 | 密钥块 | 密钥长度 | 说明 |
|------|-------------|--------|---------|------|
| ESP32 | 4 个 (BLOCK0-3) | BLOCK1-3 (3个) | 256-bit | BLOCK1=Flash加密, BLOCK2=安全启动, BLOCK3=用户数据 |
| ESP32-S2/S3 | 多个 | KEY0-KEY5 (6个) | 256-bit | 统一密钥管理器，支持用途标记 |
| ESP32-C2 | 4 个 (BLOCK0-3) | BLOCK_KEY0 (1个) | 256-bit | 只有 BLOCK_KEY0 用于密钥存储 |
| ESP32-C3/C6 | 多个 | KEY0-KEY5 (6个) | 256-bit | 标准新架构，完整支持安全启动 v2 |

FakeEsptool 当前状态：
- eFuse 数组可存放密钥（动态分配的字节数组）
- .esp 设备文件格式已包含 eFuse 数据，密钥可持久化保存

### 开发模式 vs 产品模式

| 模式 | eFuse 标志 | 行为 | 安全性 |
|------|-----------|------|--------|
| 开发模式 | `DISABLE_DL_ENCRYPT=0` | 允许选择是否使用 `--encrypt` | 低（便于调试） |
| 产品模式 | `DISABLE_DL_ENCRYPT=1` | 强制使用 `--encrypt`，禁止明文烧录 | 高（防止固件泄露） |

**注意**：切换到产品模式是不可逆的（烧录 eFuse 位）。

### 设备加密状态

设备加密有 3 种状态，通过 eFuse 字段判断：

| 状态 | flash_crypt_cnt | DISABLE_DL_ENCRYPT | 行为 |
|------|-----------------|-------------------|------|
| **无加密** | 偶数个 1 位 (0, 2, 4...) | 0 | 可明文烧录 |
| **开发模式** | 奇数个 1 位 (1, 3, 5...) | 0 | 已加密但允许明文烧录 |
| **产品模式** | 奇数个 1 位 (1, 3, 5...) | 1 | 已加密且禁止明文烧录 |

**判断函数**（参考 esptool `targets/esp32.py`）：

```python
def get_flash_encryption_enabled(self):
    """检查 flash_crypt_cnt 的二进制中 1 的个数是否为奇数"""
    flash_crypt_cnt = self.read_reg(EFUSE_SPI_BOOT_CRYPT_CNT_REG)
    return bin(flash_crypt_cnt).count("1") & 1 != 0

def get_encrypted_download_disabled(self):
    """检查 DISABLE_DL_ENCRYPT eFuse 位"""
    return self.read_reg(EFUSE_DIS_DOWNLOAD_MANUAL_ENCRYPT_REG) & EFUSE_DIS_DOWNLOAD_MANUAL_ENCRYPT
```

**关键 eFuse 字段**：

| 字段 | 说明 | 来源 |
|------|------|------|
| `flash_crypt_cnt` | 加密计数器（位计数） | GET_SECURITY_INFO 响应 |
| `DISABLE_DL_ENCRYPT` | 禁用下载加密 | eFuse BLOCK0 |

**esptool 烧录时的检查逻辑**：

```python
# 产品模式检查（禁止明文烧录）
if get_encrypted_download_disabled() and get_flash_encryption_enabled():
    raise FatalError("Flash encryption enabled and download manual encrypt disabled.\n"
                     "Data must be encrypted appropriately before flashing.")
```

**FakeEsptool 实现需求**：
- [ ] 在 eFuse 中定义 `flash_crypt_cnt` 和 `DISABLE_DL_ENCRYPT` 字段位置
- [ ] 实现 `get_flash_encryption_enabled()` 和 `get_encrypted_download_disabled()` 函数
- [ ] 在 GET_SECURITY_INFO 响应中返回正确的 `flash_crypt_cnt` 值
- [ ] 在 Key Management 对话框中显示加密状态
- [ ] 在 FLASH_BEGIN/FLASH_DEFL_BEGIN 处理中检查加密状态

### 当前状态

| 处理函数 | encrypted 解析 | 日志输出 |
|----------|---------------|---------|
| HandleFlashBegin() | ✅ 已解析 | ✅ 已输出 |
| HandleFlashDeflBegin() | ❌ 未解析 | ❌ 未输出 |

### 实现内容

**协议层（esptool.c）**：
- `HandleFlashDeflBegin()` 添加 encrypted 字段解析
  - 检查 `pkt->size >= 20`
  - 读取 `pkt->data[16..19]` 作为 encrypted 标志
  - 添加日志输出 encrypted 字段
- 统一两个处理器的日志格式

**加密/解密实现**：
- 参考 deflate 模块的实现方式，自行实现加解密算法
- 新增源文件：`src/utils/encrypt.c` 和 `src/utils/encrypt.h`
- 实现加密写入：使用 eFuse 密钥加密数据后写入 Flash
- 实现解密读取：READ_FLASH 返回解密后的明文数据
- 加解密算法：AES-XTS（参考 espsecure 的 `_flash_encryption_operation_esp32` 实现）

**Key Management 对话框（dlg/key_mgmt.c）**：
- 新增源文件：`src/dlg/key_mgmt.c` 和 `src/dlg/key_mgmt.h`
- 列表控件：显示所有可用密钥块（根据芯片类型动态显示）
- 列：Block、Purpose、Status、Size
- 按钮：Import、Export、Generate、Close
- 功能：
  - Import：从 .bin 文件导入密钥到选中的密钥块
  - Export：从选中的密钥块导出密钥到 .bin 文件
  - Generate：生成随机密钥并写入选中的密钥块
- 状态逻辑：串口连接时禁用

**菜单更新（app_commands.c）**：
- Flash 菜单添加 "Key Management..." 选项
- 打开 Key Management 对话框

**测试（tests/test_encrypt.c）**：
- 参考 `tests/test_deflate.c` 的测试框架
- 测试 AES-XTS 加密/解密正确性
- 测试与 espsecure 生成的加密数据的兼容性
- 测试各芯片的密钥长度和加密参数
- 测试边界条件和错误处理

**构建配置（tests/CMakeLists.txt）**：
- 添加 `test_encrypt` 测试目标
- 链接 `../src/utils/encrypt.c` 源文件

**文档更新**：
- PROTOCOL.md: 更新 FLASH_BEGIN 和 FLASH_DEFL_BEGIN 的请求格式说明
- DEVELOPMENT.md: 更新协议命令说明

### 不需要实现的内容

- MD5 校验：加密烧录时客户端会跳过 MD5 验证
- espsecure 工具：离线工具，不与设备通信

### Dump/导出行为

| 功能 | 行为 | 说明 |
|------|------|------|
| READ_FLASH | 返回解密后的明文 | 与真实芯片一致（硬件解密） |
| Dump 设备 | 导出加密数据 | Flash 原始内容 |
| Export Flash | 导出加密数据 | Flash 原始内容 |

### 测试方法

**ESP32-S2/S3/C2/C3/C6（ROM 模式支持）**：
```bash
esptool --port COM10 --encrypt write-flash 0x0 firmware.bin
```

**ESP32（需要 Stub 模式）**：
```bash
esptool --port COM10 --encrypt write-flash 0x0 firmware.bin
```
注意：ESP32 在 ROM 模式下不会发送 encrypted 标志，需要 Stub 模式才能测试加密烧录。

**完整测试流程**：
```bash
# 1. 生成密钥（离线）
espsecure generate-flash-encryption-key -k 256 key.bin

# 2. 烧录密钥到 FakeEsptool 的 eFuse
espefuse --port COM10 --chip esp32c3 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 加密烧录
esptool --port COM10 --encrypt write-flash 0x0 firmware.bin
```

验证 FakeEsptool 日志中是否正确显示 `encrypted=1`（ESP32 Stub 模式或其他芯片 ROM 模式）。

**单元测试**：`tests/test_encrypt.c`

测试内容：
- AES-XTS 加密/解密正确性验证
- 与 espsecure 生成的加密数据兼容性测试
- 各芯片密钥长度和加密参数测试
- 边界条件和错误处理测试

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

## 中优先级 - 加密状态与下载模式模拟

在串口协议端模拟加密状态（开发/产品模式）和下载模式（安全/禁用）的真实逻辑处理。

### 功能概述

FakeEsptool 需要模拟以下状态，并在状态栏显示：

| 状态栏 | 状态 | 说明 |
|--------|------|------|
| 加密状态 | `No Encryption` | Flash 未加密 |
| | `Encrypted (Dev)` | Flash 已加密，开发模式（允许明文烧录） |
| | `Encrypted (Prod)` | Flash 已加密，产品模式（禁止明文烧录） |
| 下载模式 | `Download Normal` | 下载模式正常 |
| | `Download Secure` | 安全下载模式（只读/只写，禁止 Stub） |
| | `Download Disabled` | 下载模式已禁用 |

### eFuse 字段映射

#### 加密状态判断字段

| 芯片 | 字段 | 偏移 | 位 | 说明 |
|------|------|------|-----|------|
| ESP32 | `FLASH_CRYPT_CNT` | BLOCK0 word0 | [26:20] | 奇数个 1 位表示加密启用 |
| ESP32-S2/S3 | `SPI_BOOT_CRYPT_CNT` | BLOCK0 word2 | [20:18] | 奇数个 1 位表示加密启用 |
| ESP32-C2 | `SPI_BOOT_CRYPT_CNT` | BLOCK0 word1 | [9:7] | 奇数个 1 位表示加密启用 |
| ESP32-C3/C6 | `SPI_BOOT_CRYPT_CNT` | BLOCK0 word2 | [20:18] | 奇数个 1 位表示加密启用 |

#### 开发/产品模式判断字段

| 芯片 | 字段 | 偏移 | 位 | 说明 |
|------|------|------|-----|------|
| ESP32 | `DISABLE_DL_ENCRYPT` | BLOCK0 word6 | [7] | 1=禁用下载加密（产品模式） |
| | `DISABLE_DL_DECRYPT` | BLOCK0 word6 | [8] | 1=禁用下载解密 |
| | `DISABLE_DL_CACHE` | BLOCK0 word6 | [9] | 1=禁用下载缓存 |
| ESP32-S2/S3 | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | BLOCK0 word1 | [20] | 1=禁用手动加密（产品模式） |
| ESP32-C2 | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | BLOCK0 word1 | [6] | 1=禁用手动加密（产品模式） |
| ESP32-C3/C6 | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | BLOCK0 word1 | [20] | 1=禁用手动加密（产品模式） |

#### 下载模式判断字段

| 芯片 | 字段 | 偏移 | 位 | 说明 |
|------|------|------|-----|------|
| ESP32 | `UART_DOWNLOAD_DIS` | BLOCK0 word0 | [27] | 1=禁用 UART 下载 |
| ESP32-S2 | `DIS_USB_DOWNLOAD_MODE` | BLOCK0 word4 | [4] | 1=禁用 USB 下载 |
| ESP32-S3 | `DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE` | BLOCK0 word4 | [4] | 1=禁用 USB-Serial-JTAG 下载 |
| ESP32-C2 | `DIS_DOWNLOAD_MODE` | BLOCK0 word1 | [14] | 1=禁用下载模式 |
| ESP32-C3 | `DIS_DOWNLOAD_MODE` | BLOCK0 word4 | [0] | 1=禁用下载模式 |
| | `DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE` | BLOCK0 word4 | [4] | 1=禁用 USB-Serial-JTAG 下载 |
| ESP32-C6 | `DIS_DOWNLOAD_MODE` | BLOCK0 word4 | [0] | 1=禁用下载模式 |
| | `DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE` | BLOCK0 word4 | [4] | 1=禁用 USB-Serial-JTAG 下载 |

#### 安全下载判断字段

| 芯片 | 字段 | 偏移 | 位 | 说明 |
|------|------|------|-----|------|
| ESP32-S2 | `ENABLE_SECURITY_DOWNLOAD` | BLOCK0 word4 | [5] | 1=启用安全下载 |
| ESP32-S3 | `ENABLE_SECURITY_DOWNLOAD` | BLOCK0 word4 | [5] | 1=启用安全下载 |
| ESP32-C2 | `ENABLE_SECURITY_DOWNLOAD` | BLOCK0 word1 | [16] | 1=启用安全下载 |
| ESP32-C3 | `ENABLE_SECURITY_DOWNLOAD` | BLOCK0 word4 | [5] | 1=启用安全下载 |
| ESP32-C6 | `ENABLE_SECURITY_DOWNLOAD` | BLOCK0 word4 | [5] | 1=启用安全下载 |

### 实现内容

#### 1. eFuse 字段定义（chip.h）

```c
/* ESP32 eFuse 字段偏移和位掩码 */
#define ESP32_FLASH_CRYPT_CNT_REG      0x00  /* BLOCK0 word0 */
#define ESP32_FLASH_CRYPT_CNT_MASK     0x7F << 20
#define ESP32_DISABLE_DL_ENCRYPT_REG   0x18  /* BLOCK0 word6 */
#define ESP32_DISABLE_DL_ENCRYPT       1 << 7
#define ESP32_UART_DOWNLOAD_DIS_REG    0x00  /* BLOCK0 word0 */
#define ESP32_UART_DOWNLOAD_DIS        1 << 27

/* ESP32-S2/S3/C3/C6 eFuse 字段偏移和位掩码 */
#define ESP32S2_SPI_BOOT_CRYPT_CNT_REG      0x08  /* BLOCK0 word2 */
#define ESP32S2_SPI_BOOT_CRYPT_CNT_MASK     0x7 << 18
#define ESP32S2_DIS_DOWNLOAD_MANUAL_ENCRYPT 1 << 20
#define ESP32S2_ENABLE_SECURITY_DOWNLOAD    1 << 5
#define ESP32S2_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE 1 << 4

/* ESP32-C2 eFuse 字段偏移和位掩码 */
#define ESP32C2_SPI_BOOT_CRYPT_CNT_REG      0x04  /* BLOCK0 word1 */
#define ESP32C2_SPI_BOOT_CRYPT_CNT_MASK     0x7 << 7
#define ESP32C2_DIS_DOWNLOAD_MANUAL_ENCRYPT 1 << 6
#define ESP32C2_ENABLE_SECURITY_DOWNLOAD    1 << 16
#define ESP32C2_DIS_DOWNLOAD_MODE           1 << 14
```

#### 2. 状态判断函数（chip.c）

```c
/* 检查 Flash 加密是否启用 */
BOOL Chip_IsFlashEncryptionEnabled(const CHIP_CTX *ctx);

/* 检查是否为产品模式（禁止明文烧录） */
BOOL Chip_IsProductionMode(const CHIP_CTX *ctx);

/* 检查下载模式是否禁用 */
BOOL Chip_IsDownloadDisabled(const CHIP_CTX *ctx);

/* 检查是否为安全下载模式 */
BOOL Chip_IsSecureDownload(const CHIP_CTX *ctx);

/* 获取加密状态字符串 ID */
UINT Chip_GetEncryptionStatusStrId(const CHIP_CTX *ctx);

/* 获取下载模式字符串 ID */
UINT Chip_GetDownloadModeStrId(const CHIP_CTX *ctx);
```

#### 3. 状态栏显示（app_commands.c）

```c
/* UpdateStatusBar 中添加 */
SendMessageW(g_hStatusbar, SB_SETTEXT, 3, (LPARAM)LoadStr(Chip_GetEncryptionStatusStrId(&g_device.chip)));
SendMessageW(g_hStatusbar, SB_SETTEXT, 4, (LPARAM)LoadStr(Chip_GetDownloadModeStrId(&g_device.chip)));
```

#### 4. GET_SECURITY_INFO 响应更新

```c
/* flash_crypt_cnt 字段从 eFuse 读取 */
sec_data[4] = Chip_GetFlashCryptCnt(ctx->chip);
```

#### 5. 烧录命令检查

```c
/* HandleFlashBegin / HandleFlashDeflBegin 中检查 */
if (Chip_IsProductionMode(ctx->chip) && !encrypted) {
    /* 产品模式下禁止明文烧录 */
    Serial_PostLog(ctx->hNotify, L"ERR", L"  Production mode: plaintext flash disabled");
    // 返回错误或警告
}
```

### 状态判断逻辑

```
加密状态判断：
  if (flash_crypt_cnt 为奇数个 1 位) {
      if (disable_dl_encrypt == 1) {
          状态 = "Encrypted (Prod)"  // 产品模式
      } else {
          状态 = "Encrypted (Dev)"   // 开发模式
      }
  } else {
      状态 = "No Encryption"         // 未加密
  }

下载模式判断：
  if (download_disabled) {
      状态 = "Download Disabled"     // 下载禁用
  } else if (security_download_enabled) {
      状态 = "Download Secure"       // 安全下载
  } else {
      状态 = "Download Normal"       // 正常
  }
```

### 测试方法

```bash
# 1. 创建设备并设置 eFuse
espefuse --port COM10 burn-efuse FLASH_CRYPT_CNT 1
espefuse --port COM10 burn-efuse DISABLE_DL_ENCRYPT 1

# 2. 检查状态栏显示
# 应显示：加密（生产）

# 3. 尝试明文烧录（应失败）
esptool --port COM10 write-flash 0x0 firmware.bin

# 4. 加密烧录（应成功）
esptool --port COM10 --encrypt write-flash 0x0 firmware.bin
```

### 协议处理逻辑

#### 1. GET_SECURITY_INFO 响应更新

烧录器通过 `GET_SECURITY_INFO` 命令获取设备安全状态，响应中包含 `flash_crypt_cnt` 字段。

```c
/* HandleGetSecurityInfo 中更新 */
static void HandleGetSecurityInfo(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    /* ... 现有代码 ... */

    /* 更新 flash_crypt_cnt 字段 */
    sec_data[4] = Chip_GetFlashCryptCnt(ctx->chip);

    /* ... 现有代码 ... */
}
```

**烧录器行为**：
- esptool 读取 `flash_crypt_cnt` 判断是否启用加密
- 如果 `flash_crypt_cnt` 为奇数个 1 位，认为加密已启用
- 如果加密已启用且未使用 `--encrypt` 参数，esptool 会警告或拒绝烧录

#### 2. 下载模式禁用处理

当 `DIS_DOWNLOAD_MODE=1` 时，设备应拒绝所有协议命令。

```c
/* Esptool_ProcessFrame 中添加检查 */
BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len)
{
    /* 检查下载模式是否禁用 */
    if (Chip_IsDownloadDisabled(ctx->chip)) {
        /* 下载模式已禁用，不响应任何命令 */
        TRACE_PROTO(TAG, "Download mode disabled, ignoring command 0x%02X", frame[1]);
        Serial_PostLog(ctx->hNotify, L"ESP", L"  Download mode disabled, command ignored");
        return FALSE;
    }

    /* ... 现有代码 ... */
}
```

**真实芯片行为**：
- ESP32：`UART_DOWNLOAD_DIS=1` 时，ROM 不响应任何 SLIP 命令
- ESP32-S2/S3/C2/C3/C6：`DIS_DOWNLOAD_MODE=1` 时，ROM 不进入下载模式

**FakeEsptool 模拟**：
- 收到 SYNC 命令时不响应（模拟 ROM 不进入下载模式）
- 收到其他命令时也不响应

#### 3. 安全下载模式处理

当 `ENABLE_SECURITY_DOWNLOAD=1` 时，设备应限制某些命令。

```c
/* Esptool_ProcessFrame 中添加检查 */
BOOL Esptool_ProcessFrame(ESPTOOL_CTX *ctx, const BYTE *frame, int frame_len)
{
    /* ... 现有代码 ... */

    /* 安全下载模式检查 */
    if (Chip_IsSecureDownload(ctx->chip)) {
        /* 安全下载模式下，只允许以下命令：
         * - SYNC (0x08)
         * - READ_REG (0x0A)
         * - WRITE_REG (0x09)
         * - SPI_ATTACH (0x0D)
         * - FLASH_BEGIN (0x02)
         * - FLASH_DATA (0x03)
         * - FLASH_END (0x04)
         * - FLASH_DEFL_BEGIN (0x10)
         * - FLASH_DEFL_DATA (0x11)
         * - FLASH_DEFL_END (0x12)
         * - SPI_SET_PARAMS (0x0B)
         * - SPI_FLASH_MD5 (0x13)
         * - CHANGE_BAUDRATE (0x0F)
         *
         * 禁止以下命令：
         * - MEM_BEGIN (0x05)
         * - MEM_DATA (0x07)
         * - MEM_END (0x06)
         * - READ_FLASH (0xD2) - stub only
         * - ERASE_FLASH (0xD0) - stub only
         * - ERASE_REGION (0xD1) - stub only
         */
        switch (pkt->command) {
        case ESP_CMD_MEM_BEGIN:
        case ESP_CMD_MEM_DATA:
        case ESP_CMD_MEM_END:
        case ESP_CMD_READ_FLASH:
        case ESP_CMD_ERASE_FLASH:
        case ESP_CMD_ERASE_REGION:
            TRACE_PROTO(TAG, "Secure download: command 0x%02X not allowed", pkt->command);
            Serial_PostLogF(ctx->hNotify, L"ESP", L"  Secure download: command 0x%02X rejected", pkt->command);
            Esptool_SendResponse(ctx, pkt->command, pkt->value, ESP_FAIL, NULL, 4);
            return FALSE;
        }
    }

    /* ... 现有代码 ... */
}
```

**真实芯片行为**：
- 安全下载模式下，ROM 只允许 Flash 烧录相关命令
- 禁止内存读写命令（防止固件提取）
- 禁止 Flash 擦除命令（防止恶意擦除）

#### 4. 产品模式明文烧录检查

当 `DISABLE_DL_ENCRYPT=1` 且 `flash_crypt_cnt` 为奇数时，禁止明文烧录。

```c
/* HandleFlashBegin / HandleFlashDeflBegin 中添加检查 */
static void HandleFlashBegin(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
{
    /* ... 现有代码 ... */

    /* 产品模式检查 */
    if (Chip_IsProductionMode(ctx->chip) && !encrypted) {
        TRACE_PROTO(TAG, "Production mode: plaintext flash not allowed");
        Serial_PostLog(ctx->hNotify, L"ERR", L"  Production mode: plaintext flash disabled");
        BYTE status_len = ESP_STATUS_LEN(ctx);
        Esptool_SendResponseEx(ctx, ESP_CMD_FLASH_BEGIN, ctx->last_read_val, ESP_FAIL, status_len, NULL, status_len);
        return;
    }

    /* ... 现有代码 ... */
}
```

**真实芯片行为**：
- 产品模式下，ROM 强制要求 `encrypted=1`
- 如果 `encrypted=0`，ROM 返回错误

#### 5. 菜单和状态栏同步更新

当通过协议命令修改 eFuse 时，需要同步更新菜单和状态栏。

```c
/* eFuse 写入后更新 UI */
void Chip_UpdateEfuse(CHIP_CTX *chip, int offset, int size, const BYTE *data)
{
    /* 写入 eFuse */
    BYTE *efuse = Chip_GetEfuseMut(chip);
    memcpy(efuse + offset, data, size);

    /* 检查是否影响加密状态或下载模式 */
    if (Chip_IsEncryptionStateChanged(chip) || Chip_IsDownloadModeChanged(chip)) {
        /* 通知主窗口更新 UI */
        PostMessage(g_hMainWnd, WM_UPDATE_STATUS, 0, 0);
    }
}
```

#### 6. 完整命令处理流程

```
收到 SLIP 帧
    │
    ▼
检查下载模式是否禁用
    │
    ├── 是 → 不响应（模拟 ROM 不进入下载模式）
    │
    ▼
检查安全下载模式
    │
    ├── 是 → 检查命令是否允许
    │         │
    │         ├── 不允许 → 返回 ESP_FAIL
    │         │
    │         ▼
    │      继续处理
    │
    ▼
解析命令
    │
    ├── FLASH_BEGIN / FLASH_DEFL_BEGIN
    │       │
    │       ▼
    │   检查产品模式
    │       │
    │       ├── 是且未加密 → 返回 ESP_FAIL
    │       │
    │       ▼
    │   正常处理
    │
    ▼
执行命令并返回响应
```
