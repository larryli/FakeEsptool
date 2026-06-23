---
layout: default
title: ESP 加密烧录
---

本文档覆盖 FakeEsptool 加密烧录功能的完整测试流程。

---

## 1. eFuse 特性说明

### 1.1 一次性可编程（OTP）

eFuse 只能将位从 **0 变为 1**，不能从 1 变为 0。这是硬件特性，FakeEsptool 正确模拟了此行为。

```
当前值: 0b001
写入值: 0b000
结果:   0b001（无变化，OR 操作）
```

### 1.2 加密计数器判断规则

加密计数器是位计数器，通过二进制中 1 的个数判断加密状态：

| 1 的个数 | 状态 | 说明 |
|----------|------|------|
| 偶数 (0, 2, 4...) | 加密禁用 | `No Encryption` |
| 奇数 (1, 3, 5...) | 加密启用 | `Encrypted (Dev)` 或 `Encrypted (Release)` |

### 1.3 各芯片加密字段对照

| 芯片 | 加密计数器 | 位宽 | 禁用明文烧录 | 禁用下载解密 | 禁用下载 | 安全下载 | 密钥块 | 密钥长度 |
|------|-----------|------|-------------|-------------|---------|---------|--------|---------|
| ESP8266 | ❌ 不支持 | - | - | - | - | - | - | - |
| ESP32 | `FLASH_CRYPT_CNT` | 7-bit | `DISABLE_DL_ENCRYPT` | `DISABLE_DL_DECRYPT` | `UART_DOWNLOAD_DIS` | ❌ | BLOCK1-3 | 256-bit |
| ESP32-S2 | `SPI_BOOT_CRYPT_CNT` | 3-bit | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | ❌ | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 | 128/256-bit |
| ESP32-S3 | `SPI_BOOT_CRYPT_CNT` | 3-bit | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | ❌ | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 | 128/256-bit |
| ESP32-C2 | `SPI_BOOT_CRYPT_CNT` | 3-bit | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | ❌ | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | BLOCK_KEY0 | 256-bit |
| ESP32-C3 | `SPI_BOOT_CRYPT_CNT` | 3-bit | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | ❌ | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 | 128/256-bit |
| ESP32-C6 | `SPI_BOOT_CRYPT_CNT` | 3-bit | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | ❌ | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 | 128/256-bit |

**说明**：
- `DISABLE_DL_DECRYPT`：仅 ESP32 支持，控制 READ_FLASH 是否返回解密数据
- 其他芯片的 READ_FLASH 解密行为由硬件自动控制（无独立字段）

### 1.4 密钥长度说明

| 密钥长度 | 参数 | 说明 |
|---------|------|------|
| 128-bit (16B) | `-k 128` | XTS-AES-128，使用 1 个 128-bit 密钥 |
| 256-bit (32B) | `-k 256` | XTS-AES-128，使用 2 个 128-bit 密钥（数据+ tweak） |
| 512-bit (64B) | `-k 512` | XTS-AES-256，使用 2 个 256-bit 密钥（ESP32-S2/S3/C3/C6） |

**FakeEsptool 支持**：256-bit 和 512-bit 密钥。

### 1.5 密钥用途说明

| 密钥用途 | 说明 | 适用芯片 |
|---------|------|---------|
| `XTS_AES_128_KEY` | Flash 加密密钥（128-bit） | ESP32-S2/S3/C2/C3/C6 |
| `XTS_AES_256_KEY_1` | Flash 加密密钥 1（256-bit） | ESP32-S2/S3/C3/C6 |
| `XTS_AES_256_KEY_2` | Flash 加密密钥 2（256-bit） | ESP32-S2/S3/C3/C6 |
| `SECURE_BOOT_DIGEST0` | 安全启动摘要 0 | ESP32-S2/S3/C3/C6 |
| `SECURE_BOOT_DIGEST1` | 安全启动摘要 1 | ESP32-S2/S3/C3/C6 |
| `SECURE_BOOT_DIGEST2` | 安全启动摘要 2 | ESP32-S2/S3/C3/C6 |
| `HMAC_DOWN_ALL` | HMAC 下行全量 | ESP32-S2/S3/C3/C6 |
| `HMAC_DOWN_JTAG` | HMAC JTAG 解锁 | ESP32-S2/S3/C3/C6 |
| `HMAC_DOWN_DIGITAL_SIGNATURE` | HMAC 数字签名 | ESP32-S2/S3/C3/C6 |
| `HMAC_UP` | HMAC 上行 | ESP32-S2/S3/C3/C6 |
| `USER` | 用户自定义 | ESP32-S2/S3/C3/C6 |

---

## 2. 加密状态矩阵

| 状态 | flash_crypt_cnt | DISABLE_DL_ENCRYPT | 行为 |
|------|----------------|-------------------|------|
| **无加密** | 偶数 (0, 2, 4...) | 0 | 允许明文烧录，允许加密烧录 |
| **开发模式** | 奇数 (1, 3, 5...) | 0 | 已加密，但允许明文烧录 |
| **量产模式** | 奇数 (1, 3, 5...) | 1 | 已加密，禁止明文烧录 |

### 2.1 开发模式 → 量产模式（不可逆）

```
1. burn-efuse SPI_BOOT_CRYPT_CNT 1    # 0→1，启用加密（开发模式）
2. burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1  # 启用量产模式
```

### 2.2 开发模式关闭加密（可逆）

```
当前: SPI_BOOT_CRYPT_CNT = 0b001 (1，奇数个 1 ，加密启用)
操作: burn-efuse SPI_BOOT_CRYPT_CNT 3  # 0b001 → 0b011 (3，偶数个 1，加密禁用)
操作: burn-efuse SPI_BOOT_CRYPT_CNT 7  # 0b011 → 0b111 (7，奇数个 1，加密启用)
操作: burn-efuse SPI_BOOT_CRYPT_CNT 6  # 0b111 → 0b111 (7，无变化)
```

**无法通过烧录将已烧录 1 的位变为 0**，只能有限次的切换加密状态。

### 2.3 量产模式下无法操作加密计数器

量产模式下（`DIS_DOWNLOAD_MANUAL_ENCRYPT=1`），加密计数器被保护，无法通过 `burn-efuse` 修改。

---

## 3. 测试流程

### 3.1 ESP32-C3/C6/S2/S3 开发模式测试

**测试目标**：验证开发模式下的加密烧录功能

```bash
# 1. 新建设备（确保 eFuse 全 0）
#    FakeEsptool: File → New Device → ESP32-C3

# 2. 生成密钥
espsecure generate-flash-encryption-key -k 256 key.bin

# 3. 烧录密钥到 eFuse
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 4. 启用开发模式（加密计数器置 1）
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 5. 验证状态栏显示 "Encrypted (Dev)"

# 6. 测试加密烧录
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：日志显示 encrypted=1，Flash 内容为密文

# 7. 测试明文烧录（开发模式允许）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：日志显示 encrypted=0，Flash 内容为明文

# 8. 测试预加密文件烧录
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 write-flash 0x0 firmware_enc.bin
# 验证：Flash 内容为预加密数据（不解密）
```

### 3.2 ESP32-C3/C6/S2/S3 量产模式测试

**测试目标**：验证量产模式下的加密烧录限制

```bash
# 1. 新建设备

# 2. 生成密钥并烧录
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用开发模式
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 4. 启用量产模式（不可逆）
espefuse --port COM10 burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1

# 5. 验证状态栏显示 "Encrypted (Release)"

# 6. 测试明文烧录（应被设备拒绝）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：设备返回 ESP_FAIL

# 7. 测试加密烧录（应被 esptool 拒绝）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：esptool 报错 "Flash encryption is enabled and download manual encrypt disabled"

# 8. 测试预加密文件 + --force
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 --force write-flash 0x0 firmware_enc.bin
# 验证：烧录成功
```

### 3.3 ESP32 开发模式测试

**测试目标**：验证 ESP32 特殊的加密烧录流程（需要 Stub 模式）

```bash
# 1. 新建设备（ESP32）

# 2. 生成密钥
espsecure generate-flash-encryption-key -k 256 key.bin

# 3. 烧录密钥到 eFuse（ESP32 使用 BLOCK1）
espefuse --port COM10 burn-key BLOCK1 key.bin

# 4. 启用开发模式
espefuse --port COM10 burn-efuse FLASH_CRYPT_CNT 1

# 5. 验证状态栏显示 "Encrypted (Dev)"

# 6. 测试加密烧录（ESP32 ROM 不支持，需要 Stub）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：esptool 自动上传 Stub，日志显示 encrypted=1

# 7. 测试明文烧录
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：日志显示 encrypted=0
```

### 3.4 ESP32 量产模式测试

**测试目标**：验证 ESP32 量产模式的加密烧录限制

```bash
# 1. 新建设备（ESP32）

# 2. 生成密钥并烧录
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK1 key.bin

# 3. 启用开发模式
espefuse --port COM10 burn-efuse FLASH_CRYPT_CNT 1

# 4. 启用量产模式
espefuse --port COM10 burn-efuse DISABLE_DL_ENCRYPT 1

# 5. 验证状态栏显示 "Encrypted (Release)"

# 6. 测试明文烧录（应被设备拒绝）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：设备返回 ESP_FAIL

# 7. 测试加密烧录（ESP32 Stub 模式支持）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：烧录成功（Stub 模式绕过量产模式检查）

# 8. 测试预加密文件 + --force
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 --force write-flash 0x0 firmware_enc.bin
# 验证：烧录成功
```

### 3.5 ESP32-C2 开发模式测试

**测试目标**：验证 ESP32-C2 的加密烧录流程

```bash
# 1. 新建设备（ESP32-C2）

# 2. 生成密钥
espsecure generate-flash-encryption-key -k 256 key.bin

# 3. 烧录密钥到 eFuse
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 4. 启用开发模式
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 5. 验证状态栏显示 "Encrypted (Dev)"

# 6. 测试加密烧录
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：日志显示 encrypted=1

# 7. 测试明文烧录（开发模式允许）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：日志显示 encrypted=0
```

### 3.6 ESP32-C2 量产模式测试

**测试目标**：验证 ESP32-C2 量产模式的加密烧录限制

```bash
# 1. 新建设备（ESP32-C2）

# 2. 生成密钥并烧录
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用开发模式
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 4. 启用量产模式
espefuse --port COM10 burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1

# 5. 验证状态栏显示 "Encrypted (Release)"

# 6. 测试明文烧录（应被设备拒绝）
esptool --port COM10 write-flash 0x0 firmware.bin
# 验证：设备返回 ESP_FAIL

# 7. 测试加密烧录（应被 esptool 拒绝）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：esptool 报错

# 8. 测试预加密文件 + --force
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool --port COM10 --force write-flash 0x0 firmware_enc.bin
# 验证：烧录成功
```

---

## 4. NVS 加密

### 4.1 NVS 加密概述

NVS (Non-Volatile Storage) 加密是 ESP-IDF 的独立安全功能，用于加密 NVS 分区中的敏感数据（如 WiFi 凭证、证书等）。

**与 Flash 加密的区别**：

| 特性 | Flash 加密 | NVS 加密 |
|------|-----------|----------|
| 加密范围 | 整个 Flash | 仅 NVS 分区 |
| 加密方式 | 透明（硬件自动） | NVS 库内部处理 |
| 密钥来源 | eFuse 中的 Flash 加密密钥 | NVS Key Partition 或 eFuse HMAC 密钥 |
| 解密时机 | 读取时自动解密 | NVS 库读取条目时解密 |

### 4.2 NVS 加密方案

ESP-IDF 支持两种 NVS 加密方案：

| 方案 | 密钥存储 | 前置条件 | 适用芯片 |
|------|---------|---------|---------|
| **Flash Encryption-Based** | Flash NVS Key Partition | 需要启用 Flash 加密 | ESP32/S2/S3/C2/C3/C6 |
| **HMAC Peripheral-Based** | eFuse HMAC 密钥（运行时派生） | 不需要 Flash 加密 | S2/S3/C3/C6 |

**注意**：
- ESP32-C2 不支持 HMAC 方案（只有 1 个 BLOCK_KEY0，无法同时用于 Flash 加密和 HMAC）
- ESP32 不支持 HMAC 方案（无 HMAC_UP 用途支持）

### 4.3 Flash Encryption-Based 方案

**工作原理**：

```
eFuse (Flash 加密密钥)
    │
    ▼
Flash 加密 ──────────────► 保护整个 Flash
    │
    ▼
NVS Key Partition (加密存储) ──► 存储 NVS XTS 密钥
    │
    ▼
NVS 数据分区 (加密存储) ──────► 存储用户数据
```

**NVS Key Partition 结构**：

```
+---------------------------------------------+
|         XTS encryption key (32 字节)         |
+---------------------------------------------+
|            XTS tweak key (32 字节)           |
+---------------------------------------------+
|                 CRC32 (4 字节)               |
+---------------------------------------------+
```

**分区表配置**：

```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
nvs_key,  data, nvs_keys,0xf000,  0x1000, encrypted
```

**密钥生成方式**：

1. **芯片自动生成**：首次启动时 `nvs_flash_init()` 自动生成并存储
2. **预生成**：使用 `nvs_partition_gen.py` 生成 NVS Key Partition

### 4.4 HMAC Peripheral-Based 方案

**工作原理**：

```
eFuse (HMAC_UP 密钥)
    │
    ▼
HMAC 派生 ──────────────► NVS XTS 密钥（运行时）
    │
    ▼
NVS 数据分区 (加密存储) ──► 存储用户数据
```

**优势**：
- 不需要启用 Flash 加密
- 不需要 NVS Key Partition
- 密钥不存储在 Flash 中（更安全）

**eFuse 密钥配置**：

```bash
# 生成 HMAC 密钥
openssl rand -out hmac_key.bin 32

# 烧录到 eFuse（使用 HMAC_UP 用途）
espefuse --port COM10 burn-key BLOCK_KEY0 hmac_key.bin HMAC_UP
```

**sdkconfig 配置**：

```
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECTION_SCHEME_HMAC=y
CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=0
```

### 4.5 各芯片 NVS 加密支持

| 芯片 | Flash Encryption-Based | HMAC-Based | NVS Key 存储位置 |
|------|----------------------|-----------|-----------------|
| ESP32 | ✅ | ❌ | NVS Key Partition |
| ESP32-S2 | ✅ | ✅ | NVS Key Partition 或 eFuse HMAC |
| ESP32-S3 | ✅ | ✅ | NVS Key Partition 或 eFuse HMAC |
| ESP32-C2 | ✅ | ❌ | NVS Key Partition（唯一方案） |
| ESP32-C3 | ✅ | ✅ | NVS Key Partition 或 eFuse HMAC |
| ESP32-C6 | ✅ | ✅ | NVS Key Partition 或 eFuse HMAC |

### 4.6 FakeEsptool 支持说明

FakeEsptool 的 Flash 加密实现已足够支持 NVS 加密：

- **Flash Encryption-Based**：NVS Key Partition 是 Flash 中的普通分区，受 Flash 加密保护
- **HMAC-Based**：NVS 密钥在运行时派生，FakeEsptool 无需特殊处理

**Dump 输出**：NVS Key Partition 的内容会包含在 Flash dump 中（已加密状态）。

---

## 5. 压缩 + 加密烧录测试

**测试目标**：验证压缩和加密同时启用的场景

```bash
# 1. 新建设备

# 2. 生成密钥并烧录
espsecure generate-flash-encryption-key -k 256 key.bin
espefuse --port COM10 burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用开发模式
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 4. 测试压缩 + 加密烧录
esptool --port COM10 write-flash --compress --encrypt 0x0 firmware.bin
# 验证：日志显示 encrypted=1，数据先解压再加密

# 5. 测试普通压缩烧录（不加密）
esptool --port COM10 write-flash --compress 0x0 firmware.bin
# 验证：日志显示 encrypted=0，数据只解压不加密
```

---

## 6. READ_FLASH 解密行为

**测试目标**：验证读取加密 Flash 时自动解密

```bash
# 1. 新建设备，启用加密并烧录加密数据

# 2. 读取 Flash（应返回解密后的明文）
esptool --port COM10 read-flash 0x0 0x1000 flash_read.bin
# 验证：flash_read.bin 内容与原始固件一致（非加密数据）

# 3. 对比
# - Flash 存储：加密数据
# - read-flash 返回：解密后的明文
# - Dump 设备：加密数据（原始 eFuse + Flash 内容）
```

**说明**：FakeEsptool 的 `READ_FLASH` 实现自动解密，与真实芯片行为一致（硬件解密引擎）。

---

## 7. Dump/Export 行为

| 操作 | 输出内容 | 说明 |
|------|---------|------|
| `Dump Device As` | 加密数据 | 原始 eFuse + Flash 内容 |
| `Export Flash` | 加密数据 | Flash 原始内容 |
| `READ_FLASH` | 解密数据 | 通过协议读取，自动解密 |
| `read-flash` 命令 | 解密数据 | esptool 调用 READ_FLASH |

**说明**：Dump/Export 导出的是设备内部的原始数据（加密状态），READ_FLASH 返回的是解密后的明文。

---

## 8. 多密钥块测试（ESP32-S2/S3/C3/C6）

**测试目标**：验证使用不同密钥块进行加密烧录

```bash
# 1. 新建设备

# 2. 生成多个密钥
espsecure generate-flash-encryption-key -k 256 key0.bin
espsecure generate-flash-encryption-key -k 256 key1.bin

# 3. 烧录到不同密钥块
espefuse --port COM10 burn-key BLOCK_KEY0 key0.bin XTS_AES_128_KEY
espefuse --port COM10 burn-key BLOCK_KEY1 key1.bin XTS_AES_128_KEY

# 4. 启用加密
espefuse --port COM10 burn-efuse SPI_BOOT_CRYPT_CNT 1

# 5. 加密烧录（使用 KEY0）
esptool --port COM10 write-flash --encrypt 0x0 firmware.bin
# 验证：使用 BLOCK_KEY0 的密钥加密
```

**说明**：FakeEsptool 默认使用 BLOCK_KEY0 进行加密烧录。

---

## 9. 禁用下载模式测试

**测试目标**：验证禁用下载模式后设备不响应命令

```bash
# 1. 新建设备

# 2. 禁用下载模式
espefuse --port COM10 burn-efuse DIS_DOWNLOAD_MODE 1

# 3. 验证状态栏显示 "Download Disabled"

# 4. 尝试连接（应超时）
esptool --port COM10 read-mac
# 验证：超时，设备不响应 SYNC
```

---

## 10. 安全下载模式测试

**测试目标**：验证安全下载模式下命令限制

```bash
# 1. 新建设备

# 2. 启用安全下载模式
espefuse --port COM10 burn-efuse DIS_DOWNLOAD_MODE 1
espefuse --port COM10 burn-efuse ENABLE_SECURITY_DOWNLOAD 1

# 3. 验证状态栏显示 "Download Secure"

# 4. 测试允许的命令
esptool --port COM10 read-mac
# 验证：成功

# 5. 测试禁止的命令（应返回错误）
# 需要通过协议测试工具发送 MEM_BEGIN 等命令
```

---

## 11. 验证检查点

### 11.1 状态栏验证

| 操作 | 预期状态栏 |
|------|-----------|
| 新建设备 | `No Encryption`, `Download Normal` |
| burn-efuse SPI_BOOT_CRYPT_CNT 1 | `Encrypted (Dev)`, `Download Normal` |
| burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1 | `Encrypted (Release)`, `Download Normal` |
| burn-efuse DIS_DOWNLOAD_MODE 1 | `*`, `Download Disabled` |
| burn-efuse ENABLE_SECURITY_DOWNLOAD 1 | `*`, `Download Secure` |

### 11.2 日志验证

| 场景 | 预期日志 |
|------|---------|
| 加密烧录 | `encrypted=1`, `Encrypted X bytes` |
| 明文烧录 | `encrypted=0`, 无加密日志 |
| 量产模式拒绝明文 | `Release mode: plaintext flash disabled` |
| 预加密文件 | `encrypted=0`, 直接写入 |

### 11.3 Flash 内容验证

| 场景 | Flash 内容 |
|------|-----------|
| 明文烧录 | 原始固件数据 |
| 加密烧录 | 加密后的数据（非原始数据） |
| 预加密文件 | 预加密数据（不解密） |
