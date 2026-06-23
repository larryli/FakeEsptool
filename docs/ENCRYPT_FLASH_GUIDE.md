# FakeEsptool - 加密烧录指南

本文档基于 ESP-IDF v6.0.1 官方文档，整理各芯片的 Flash 加密特性与 FakeEsptool 的测试流程。

参考文档：
- [ESP32](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/security/security-features-enablement-workflows.html)
- [ESP32-S2](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s2/security/security-features-enablement-workflows.html)
- [ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/security/security-features-enablement-workflows.html)
- [ESP32-C2](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c2/security/security-features-enablement-workflows.html)
- [ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c3/security/security-features-enablement-workflows.html)
- [ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c6/security/security-features-enablement-workflows.html)

---

## 1. eFuse 基础

### 1.1 一次性可编程（OTP）

eFuse 只能将位从 **0 变为 1**，不能从 1 变为 0。FakeEsptool 正确模拟了此行为。

### 1.2 加密计数器判断规则

加密计数器是位计数器，通过**二进制中 1 的个数**（奇偶性）判断状态，而非十进制值：

| 1 的个数 | 状态 | 说明 |
|----------|------|------|
| 偶数 (0, 2, 4...) | 加密禁用 | `No Encryption` |
| 奇数 (1, 3, 5...) | 加密启用 | `Encrypted (Dev)` 或 `Encrypted (Release)` |

实际烧录的值（逐位置位）：

| ESP32 (7-bit) | ESP32-S2/S3/C2/C3/C6 (3-bit) | 1 的个数 | 状态 |
|---------------|------------------------------|----------|------|
| 0 (0b0000000) | 0 (0b000) | 0 | 禁用 |
| 1 (0b0000001) | 1 (0b001) | 1 | 启用 |
| 3 (0b0000011) | 3 (0b011) | 2 | 禁用 |
| 7 (0b0000111) | 7 (0b111) | 3 | 启用 |
| 15 (0b0001111) | - | 4 | 禁用 |
| 31 (0b0011111) | - | 5 | 启用 |
| 63 (0b0111111) | - | 6 | 禁用 |
| 127 (0b1111111) | - | 7 | 启用 |

量产模式通过烧录量产模式 eFuse 启用（**不可逆**），量产模式下加密计数器被保护无法修改。

开发模式下可通过烧录更多位改变奇偶性来回退加密（如 ESP32: 1→3，S2/S3/C2/C3/C6: 1→3）。

---

## 2. 各芯片加密特性

芯片按 Flash 加密行为分为 4 组：

| 分组 | 芯片 | 特点 |
|------|------|------|
| **A** | ESP32 | 固定密钥块，7-bit 计数器，需 FLASH_CRYPT_CONFIG |
| **B** | ESP32-S2/S3 | 6 密钥块，支持 128/256/512-bit |
| **C** | ESP32-C2 | 仅 1 个密钥块，支持 128-bit 派生 |
| **D** | ESP32-C3/C6 | 6 密钥块，仅 256-bit，BLOCK_KEY5 硬件 bug |

### 2.1 对照表

| 特性 | A: ESP32 | B: ESP32-S2/S3 | C: ESP32-C2 | D: ESP32-C3/C6 |
|------|----------|----------------|-------------|----------------|
| 密钥存储 | BLOCK1（固定） | KEY0~KEY5 | BLOCK_KEY0（固定） | KEY0~KEY5 |
| 密钥长度 | 256-bit | 128/256/512-bit | 128/256-bit | 256-bit |
| 密钥用途 | `flash_encryption` | `XTS_AES_128_KEY` / `XTS_AES_256_KEY` | `XTS_AES_128_KEY` / `XTS_AES_128_KEY_DERIVED_FROM_128_EFUSE_BITS` | `XTS_AES_128_KEY` |
| 加密计数器 | `FLASH_CRYPT_CNT` (7-bit) | `SPI_BOOT_CRYPT_CNT` (3-bit) | `SPI_BOOT_CRYPT_CNT` (3-bit) | `SPI_BOOT_CRYPT_CNT` (3-bit) |
| 量产模式 eFuse | `DISABLE_DL_ENCRYPT` | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | `DIS_DOWNLOAD_MANUAL_ENCRYPT` |
| BLOCK_KEY5 XTS 限制 | 无（无 KEY5） | **S3 有 bug** | 无（仅 1 个块） | **有 bug** |
| 安全下载 | `UART_DOWNLOAD_DIS` | `ENABLE_SECURITY_DOWNLOAD` | `ENABLE_SECURITY_DOWNLOAD` | `ENABLE_SECURITY_DOWNLOAD` |
| FLASH_CRYPT_CONFIG | 需要（0xF） | 无 | 无 | 无 |
| 安全 eFuse | `JTAG_DISABLE` | `HARD_DIS_JTAG` | `DIS_PAD_JTAG` | `DIS_PAD_JTAG` |

### 2.2 S2 与 S3 的差异

| 差异 | ESP32-S2 | ESP32-S3 |
|------|----------|----------|
| BLOCK_KEY5 XTS 限制 | 无 | **不可用**（硬件 bug） |
| DIS_DOWNLOAD_DCACHE | 无 | 有 |

### 2.3 BLOCK_KEY5 硬件 bug

ESP32-S3、ESP32-C3、ESP32-C6 的 BLOCK9（BLOCK_KEY5）存在硬件 bug，不能用于 XTS_AES 密钥：

```
KEY_PURPOSE_5 can not have XTS_AES_128_KEY key due to a hardware bug
```

受影响芯片：ESP32-S3、ESP32-C3、ESP32-C6、ESP32-H2、ESP32-H4。

---

## 3. 开发模式测试

### 3.1 S2/S3/C3/C6 开发模式测试

```bash
# 1. 新建设备（eFuse 全 0）

# 2. 生成密钥并烧录
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用开发模式
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 4. 验证状态栏显示 "Encrypted (Dev)"

# 5. 测试加密烧录
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：日志显示 encrypted=1，Flash 内容为密文

# 6. 测试明文烧录（开发模式允许）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：日志显示 encrypted=0，Flash 内容为明文

# 7. 测试预加密文件烧录
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 write-flash 0x0 firmware_enc.bin
# 验证：Flash 内容为预加密数据（不解密）

# 8. 测试加密退回未加密（烧录更多位改变奇偶性）
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 3  # 0b001→0b011，1位→2位，奇→偶
# 验证：状态栏显示 "No Encryption"
```

**芯片差异**：
- **S3**：BLOCK_KEY5 不能用于 XTS_AES（硬件 bug），使用 KEY0~KEY4
- **C3/C6**：仅支持 256-bit 密钥，BLOCK_KEY5 不能用于 XTS_AES
- **S2/S3** 支持 512-bit XTS-AES-256 密钥（`--keylen 512` + `XTS_AES_256_KEY`）

### 3.2 ESP32 开发模式测试

```bash
# 1. 新建设备

# 2. 生成密钥并烧录（ESP32 使用 BLOCK1，用途固定）
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key flash_encryption key.bin

# 3. 设置 FLASH_CRYPT_CONFIG（ESP32 必须）
espefuse --port COM10 burn-efuse FLASH_CRYPT_CONFIG 0xF

# 4. 启用开发模式（FLASH_CRYPT_CNT 7-bit）
espefuse --port COM10 burn-efuse FLASH_CRYPT_CNT 1

# 5. 测试加密烧录（ESP32 ROM 不支持，需要 Stub）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：esptool 自动上传 Stub，日志显示 encrypted=1

# 6. 测试预加密文件烧录
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 write-flash 0x0 firmware_enc.bin
# 验证：Flash 内容为预加密数据（不解密）

# 7. 测试加密退回未加密（烧录更多位改变奇偶性）
espefuse --port COM10 burn-efuse FLASH_CRYPT_CNT 3  # 0b001→0b011，1位→2位，奇→偶
# 验证：状态栏显示 "No Encryption"
```

### 3.3 ESP32-C2 开发模式测试

```bash
# 1. 新建设备

# 2. 生成密钥并烧录（仅 BLOCK_KEY0 可用）
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用开发模式
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 4. 测试加密烧录
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：日志显示 encrypted=1

# 5. 测试预加密文件烧录
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 write-flash 0x0 firmware_enc.bin
# 验证：Flash 内容为预加密数据（不解密）

# 6. 测试加密退回未加密（烧录更多位改变奇偶性）
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 3  # 0b001→0b011，1位→2位，奇→偶
# 验证：状态栏显示 "No Encryption"
```

**芯片差异**：仅 1 个密钥块，支持 128-bit 派生密钥（`XTS_AES_128_KEY_DERIVED_FROM_128_EFUSE_BITS`）。

---

## 4. 量产模式测试

### 4.1 S2/S3/C3/C6 量产模式测试

```bash
# 1. 新建设备

# 2. 生成密钥并烧录
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用加密（SPI_BOOT_CRYPT_CNT 设为 7，3 位全置 1）
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 7

# 4. 启用量产模式（不可逆）
espefuse --port COM10 burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1

# 5. 验证状态栏显示 "Encrypted (Release)"

# 6. 测试明文烧录（应被设备拒绝）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：设备返回 ESP_FAIL，日志显示 Release mode: plaintext flash disabled

# 7. 测试加密烧录（应被 esptool 拒绝）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：esptool 报错 "Flash encryption is enabled and download manual encrypt disabled"

# 8. 测试预加密文件 + --force
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 --force write-flash 0x0 firmware_enc.bin
# 验证：烧录成功
```

### 4.2 ESP32 量产模式测试

```bash
# 1. 新建设备

# 2. 生成密钥并烧录（ESP32 使用 BLOCK1，用途固定）
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key flash_encryption key.bin

# 3. 设置 FLASH_CRYPT_CONFIG（ESP32 必须）
espefuse --port COM10 burn-efuse FLASH_CRYPT_CONFIG 0xF

# 4. 启用加密（FLASH_CRYPT_CNT 设为 127，7 位全置 1）
espefuse --port COM10 burn-efuse FLASH_CRYPT_CNT 127

# 5. 启用量产模式
espefuse --port COM10 burn-efuse DISABLE_DL_ENCRYPT 1

# 6. 测试明文烧录（应被设备拒绝）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：设备返回 ESP_FAIL

# 7. 测试加密烧录（ESP32 Stub 模式支持）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：烧录成功（Stub 模式绕过量产模式检查）
```

### 4.3 ESP32-C2 量产模式测试

```bash
# 1. 新建设备

# 2. 生成密钥并烧录（仅 BLOCK_KEY0 可用）
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用加密（SPI_BOOT_CRYPT_CNT 设为 7，3 位全置 1）
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 7

# 4. 启用量产模式
espefuse --port COM10 burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1

# 5. 测试明文烧录（应被设备拒绝）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：设备返回 ESP_FAIL
```

---

## 5. 压缩 + 加密烧录

```bash
# 前提：已启用开发模式

# 测试压缩 + 加密烧录
esptool --port COM10 write-flash --compress --encrypt 0x0 firmware.bin
# 验证：日志显示 encrypted=1，数据先解压再加密

# 测试普通压缩烧录（不加密）
esptool --port COM10 write-flash --compress 0x0 firmware.bin
# 验证：日志显示 encrypted=0，数据只解压不加密
```

---

## 6. READ_FLASH 解密行为

```bash
# 1. 新建设备，启用加密并烧录加密数据

# 2. 读取 Flash（应返回解密后的明文）
esptool --port COM10 read-flash 0x0 0x1000 flash_read.bin
# 验证：flash_read.bin 内容与原始固件一致（非加密数据）
```

**说明**：READ_FLASH 返回解密后的明文，与真实芯片的硬件解密引擎行为一致。

---

## 7. Dump/Export 行为

| 操作 | 输出内容 | 说明 |
|------|---------|------|
| `Dump Device As` | 加密数据 | 原始 eFuse + Flash 内容 |
| `Export Flash` | 加密数据 | Flash 原始内容 |
| `READ_FLASH` | 解密数据 | 通过协议读取，自动解密 |
| `read-flash` 命令 | 解密数据 | esptool 调用 READ_FLASH |

---

## 8. NVS 加密

### 8.1 概述

NVS 加密是 ESP-IDF 的独立安全功能，用于加密 NVS 分区中的敏感数据。

| 特性 | Flash 加密 | NVS 加密 |
|------|-----------|----------|
| 加密范围 | 整个 Flash | 仅 NVS 分区 |
| 加密方式 | 透明（硬件自动） | NVS 库内部处理 |
| 密钥来源 | eFuse 中的 Flash 加密密钥 | NVS Key Partition 或 eFuse HMAC 密钥 |

### 8.2 各芯片 NVS 加密支持

| 芯片 | Flash Encryption-Based | HMAC-Based |
|------|----------------------|-----------|
| ESP32 | ✅ | ❌ |
| ESP32-S2/S3 | ✅ | ✅ |
| ESP32-C2 | ✅ | ❌ |
| ESP32-C3/C6 | ✅ | ✅ |

- **Flash Encryption-Based**：需要启用 Flash 加密，NVS Key Partition 存储在 Flash 中并受 Flash 加密保护
- **HMAC-Based**：不需要 Flash 加密，密钥在运行时通过 eFuse HMAC 密钥派生
- ESP32 和 ESP32-C2 不支持 HMAC 方案

---

## 9. 验证检查点

### 9.1 状态栏验证

| 操作 | 预期状态栏 |
|------|-----------|
| 新建设备 | `No Encryption`, `Download Normal` |
| 启用开发模式 | `Encrypted (Dev)`, `Download Normal` |
| 启用量产模式 | `Encrypted (Release)`, `Download Normal` |
| 禁用下载模式 | `*`, `Download Disabled` |
| 启用安全下载 | `*`, `Download Secure` |

### 9.2 日志验证

| 场景 | 预期日志 |
|------|---------|
| 加密烧录 | `encrypted=1`, `Encrypted X bytes` |
| 明文烧录 | `encrypted=0`, 无加密日志 |
| 量产模式拒绝明文 | `Release mode: plaintext flash disabled` |
| 预加密文件 | `encrypted=0`, 直接写入 |

### 9.3 Flash 内容验证

| 场景 | Flash 内容 |
|------|-----------|
| 明文烧录 | 原始固件数据 |
| 加密烧录 | 加密后的数据（非原始数据） |
| 预加密文件 | 预加密数据（不解密） |
