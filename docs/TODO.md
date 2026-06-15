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
1. espsecure generate-flash-encryption-key -k 256 key.bin   # 生成密钥（离线，不与设备通信）
2. espefuse burn_key flash_encryption key.bin                # 烧录密钥到 eFuse（通过 READ_REG/WRITE_REG）
3. esptool --encrypt write_flash 0x0 firmware.bin            # 加密烧录（设备端加密后写入 Flash）
```

| 工具 | 功能 | 与设备通信 | FakeEsptool 支持状态 |
|------|------|-----------|-------------------|
| espsecure | 密钥生成、数据加密/解密 | ❌ 离线工具 | 不需要支持 |
| espefuse | eFuse 读写、密钥烧录 | ✅ READ_REG/WRITE_REG | ✅ 已支持 |
| esptool | Flash 烧录 | ✅ SLIP 协议 | ⚠️ 需要添加 encrypted 支持 |

### 密钥存储

加密密钥存储在芯片的 eFuse（一次性可编程存储器）中：

| 芯片 | 密钥 eFuse 块 | 密钥长度 |
|------|--------------|---------|
| ESP32 | BLOCK1 (1个) | 256-bit |
| ESP32-S2/S3 | KEY0-KEY5 (6个) | 256-bit |
| ESP32-C2 | KEY0 (1个) | 128-bit |
| ESP32-C3/C6 | KEY0-KEY5 (6个) | 256-bit |

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
esptool.py --port COM10 --encrypt write_flash 0x0 firmware.bin
```

**ESP32（需要 Stub 模式）**：
```bash
esptool.py --port COM10 --encrypt write_flash 0x0 firmware.bin
```
注意：ESP32 在 ROM 模式下不会发送 encrypted 标志，需要 Stub 模式才能测试加密烧录。

**完整测试流程**：
```bash
# 1. 生成密钥（离线）
espsecure generate-flash-encryption-key -k 256 key.bin

# 2. 烧录密钥到 FakeEsptool 的 eFuse
espefuse.py --port COM10 burn_key flash_encryption key.bin

# 3. 加密烧录
esptool.py --port COM10 --encrypt write_flash 0x0 firmware.bin
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
