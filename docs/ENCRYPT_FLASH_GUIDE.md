# FakeEsptool - 加密烧录测试指南

加密烧录测试流程：生成密钥 → 烧录密钥 → 加密烧录 → 验证加密状态。

---

## 快速开始：开发模式测试

以 ESP32-C3 为例，完整测试加密烧录的开发模式流程。

```bash
# 1. 生成密钥
espsecure generate-flash-encryption-key -k 256 key.bin

# 2. 烧录密钥到 eFuse
espefuse burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用开发模式（加密计数器置 1）
espefuse burn-efuse SPI_BOOT_CRYPT_CNT 1

# 4. 加密烧录（设备端加密）
esptool write-flash --encrypt 0x0 firmware.bin

# 5. 预加密文件烧录（离线加密后直接写入）
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin
esptool write-flash 0x0 firmware_enc.bin

# 6. 关闭加密（FakeEsptool 模拟）
espefuse burn-efuse SPI_BOOT_CRYPT_CNT 0
```

**ESP32 使用 `FLASH_CRYPT_CNT` 替代 `SPI_BOOT_CRYPT_CNT`，且需要 Stub 模式（esptool 自动上传）。**

---

## 快速开始：产品模式测试

以 ESP32-C3 为例，完整测试加密烧录的产品模式流程。

```bash
# 1. 生成密钥
espsecure generate-flash-encryption-key -k 256 key.bin

# 2. 烧录密钥到 eFuse
espefuse burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY

# 3. 启用开发模式（前置步骤）
espefuse burn-efuse SPI_BOOT_CRYPT_CNT 1

# 4. 启用产品模式（禁用手动加密）
espefuse burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1

# 5. 预加密文件
espsecure encrypt-flash-data -k key.bin -a 0x0 -o firmware_enc.bin firmware.bin

# 6. 强制烧录预加密文件（--force 跳过产品模式保护检查）
esptool --force write-flash 0x0 firmware_enc.bin

# 7. 关闭加密（FakeEsptool 模拟）
espefuse burn-efuse SPI_BOOT_CRYPT_CNT 0
```

**ESP32 使用 `FLASH_CRYPT_CNT` / `DISABLE_DL_ENCRYPT` 替代上述字段。**

**注意：** 步骤 5-6 中若不加 `--force`，esptool 会拒绝烧录。若不预加密而直接用 `--encrypt`，同样被拒绝（产品模式禁用了手动加密）。

---

## 1. 测试前提

启动 FakeEsptool，选择芯片，连接串口进入下载模式。

---

## 2. 生成密钥

离线生成 256-bit Flash 加密密钥，不与设备通信：

```bash
espsecure generate-flash-encryption-key -k 256 key.bin
```

生成的 `key.bin` 为 32 字节随机数据。各芯片通用。

---

## 3. 烧录密钥到 eFuse

通过 espefuse 将密钥写入 FakeEsptool 的 eFuse 存储。

**burn-key 语法（ESP32-C3/C2/C6/S2/S3）：**

```bash
espefuse burn-key BLOCK_KEY0 key.bin XTS_AES_128_KEY
```

参数说明：
- `BLOCK_KEY0`：密钥块名称（Flash 加密使用 BLOCK_KEY0）
- `key.bin`：密钥文件（32 字节）
- `XTS_AES_128_KEY`：密钥用途（Flash 加密）

**ESP32（语法不同，仅需 2 参数）：**

```bash
espefuse burn-key BLOCK1 key.bin
```

**验证：** FakeEsptool 日志中应出现多条 `WRITE_REG` 操作，目标地址在芯片 eFuse 密钥块范围内。

---

## 4. 加密烧录

使用 `--encrypt` 参数烧录，客户端发送明文，设备端加密后写入 Flash：

```bash
esptool write-flash --encrypt 0x0 firmware.bin
```

**验证：** FakeEsptool 日志中 `FLASH_BEGIN` 或 `FLASH_DEFL_BEGIN` 应显示 `encrypted=1`。

对比普通烧录（不加密）：

```bash
esptool write-flash 0x0 firmware.bin
```

日志应显示 `encrypted=0`。

**注意：** ESP32 ROM 模式不发送 `encrypted` 字段（请求仅 16 字节），需要 Stub 模式才能测试。esptool 会自动上传 Stub。

---

## 5. 开发模式加密

烧录加密计数器，启用加密但允许明文烧录：

**ESP32-S2/S3/C2/C3/C6：**

```bash
espefuse burn-efuse SPI_BOOT_CRYPT_CNT 1
```

**ESP32：**

```bash
espefuse burn-efuse FLASH_CRYPT_CNT 1
```

**预期状态栏：** `Encrypted (Dev)`

**行为：**
- 允许明文烧录（`write-flash` 不带 `--encrypt`）
- 允许加密烧录（`write-flash --encrypt`）

---

## 6. 产品模式加密

在开发模式基础上，烧录禁用明文烧录的 eFuse 位：

**ESP32：**

```bash
espefuse burn-efuse DISABLE_DL_ENCRYPT 1
```

**ESP32-S2/S3/C3/C6：**

```bash
espefuse burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1
```

**ESP32-C2：**

```bash
espefuse burn-efuse DIS_DOWNLOAD_MANUAL_ENCRYPT 1
```

**预期状态栏：** `Encrypted (Prod)`

**行为：**
- `--encrypt` 被 esptool 拒绝（产品模式禁用了"手动加密"功能）
- 明文烧录被设备拒绝
- 必须使用预加密文件（见§7）

**注意：** 烧录 eFuse 是不可逆操作。

---

## 7. 预加密文件烧录

使用 `espsecure` 离线加密固件，再将加密后的文件烧录到设备。

```bash
# 离线加密（不与设备通信）
espsecure encrypt-flash-data -k key.bin -a 0x10000 -o firmware_enc.bin firmware.bin
```

开发模式和产品模式下的行为不同：

### 开发模式

预加密文件 + 不加 `--encrypt` → 设备收到 `encrypted=0`，直接写入 Flash，不做二次加密。✅ 正确。

```bash
esptool write-flash 0x10000 firmware_enc.bin
```

也可用 `--encrypt` 让设备端加密明文，效果相同。

### 产品模式

产品模式下 `--encrypt` 被 esptool 禁止，只能用预加密文件。但设备收到 `encrypted=0` 会将数据视为明文，硬件加密引擎自动加密后再写入 → **二次加密，数据损坏**。

正确做法是用 `--force` 跳过 esptool 的保护检查：

```bash
esptool --force write-flash 0x10000 firmware_enc.bin
```

或者产品设备不走串口烧录，使用产线烧录器直接写 Flash。

### 对比总结

| 模式 | `--encrypt` | 预加密 + 无 `--encrypt` | 预加密 + `--force` |
|------|------------|----------------------|-------------------|
| 开发模式 | ✅ 设备端加密 | ✅ 直接写入 | ✅ 直接写入 |
| 产品模式 | ❌ esptool 拒绝 | ❌ 二次加密损坏 | ✅ 跳过检查 |

---

## 8. 关闭加密

将加密计数器清零（偶数个 1 位 = 加密禁用）：

**ESP32：**

```bash
espefuse burn-efuse FLASH_CRYPT_CNT 0
```

**ESP32-S2/S3/C2/C3/C6：**

```bash
espefuse burn-efuse SPI_BOOT_CRYPT_CNT 0
```

**预期状态栏：** `No Encryption`

**注意：** 实际芯片上加密计数器无法清零（只能累加），此处为 FakeEsptool 模拟行为。

---

## 9. 禁用下载模式

烧录后设备不再响应任何 SLIP 命令（模拟 ROM 不进入下载模式）。

**ESP32：**

```bash
espefuse burn-efuse UART_DOWNLOAD_DIS 1
```

**ESP32-S2/S3/C2/C3/C6：**

```bash
espefuse burn-efuse DIS_DOWNLOAD_MODE 1
```

**预期状态栏：** `Download Disabled`

**行为：** 设备不响应 SYNC 及所有后续命令，esptool 报超时。

**注意：** 此操作不可逆，烧录后将无法通过串口与设备通信。

---

## 10. 安全下载模式

限制可用命令，仅允许 Flash 烧录相关操作，禁止内存读写（防止固件提取）。

```bash
# 先启用下载模式禁用（安全下载的前提）
espefuse burn-efuse DIS_DOWNLOAD_MODE 1

# 再启用安全下载
espefuse burn-efuse ENABLE_SECURITY_DOWNLOAD 1
```

**预期状态栏：** `Download Secure`

**行为：**

| 允许 | 禁止 |
|------|------|
| SYNC, READ_REG, WRITE_REG | MEM_BEGIN, MEM_DATA, MEM_END |
| FLASH_BEGIN/DATA/END | READ_FLASH, ERASE_FLASH, ERASE_REGION |
| FLASH_DEFL_BEGIN/DATA/END | |
| SPI_SET_PARAMS, SPI_FLASH_MD5 | |
| CHANGE_BAUDRATE, SPI_ATTACH | |

禁止的命令返回 `ESP_FAIL`。

**ESP32 不支持安全下载模式**（无 `ENABLE_SECURITY_DOWNLOAD` 字段）。

---

## 11. 各芯片 eFuse 字段对照

| 芯片 | 加密计数器 | 禁用明文烧录 | 禁用下载 | 安全下载 | 密钥块 |
|------|-----------|-------------|---------|---------|--------|
| ESP32 | `FLASH_CRYPT_CNT` (7-bit) | `DISABLE_DL_ENCRYPT` | `UART_DOWNLOAD_DIS` | ❌ 不支持 | BLOCK1-3 |
| ESP32-S2 | `SPI_BOOT_CRYPT_CNT` (3-bit) | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 |
| ESP32-S3 | `SPI_BOOT_CRYPT_CNT` (3-bit) | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 |
| ESP32-C2 | `SPI_BOOT_CRYPT_CNT` (3-bit) | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0 |
| ESP32-C3 | `SPI_BOOT_CRYPT_CNT` (3-bit) | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 |
| ESP32-C6 | `SPI_BOOT_CRYPT_CNT` (3-bit) | `DIS_DOWNLOAD_MANUAL_ENCRYPT` | `DIS_DOWNLOAD_MODE` | `ENABLE_SECURITY_DOWNLOAD` | KEY0-KEY5 |

---

## 12. burn-key 密钥用途对照

| 密钥用途 | 说明 | 适用芯片 |
|----------|------|----------|
| `XTS_AES_128_KEY` | Flash 加密密钥 | ESP32-S2/S3/C2/C3/C6 |
| `SECURE_BOOT_DIGEST0` | 安全启动摘要 0 | ESP32-S2/S3/C3/C6 |
| `SECURE_BOOT_DIGEST1` | 安全启动摘要 1 | ESP32-S2/S3/C3/C6 |
| `SECURE_BOOT_DIGEST2` | 安全启动摘要 2 | ESP32-S2/S3/C3/C6 |
| `HMAC_DOWN_ALL` | HMAC 下行全量 | ESP32-S2/S3/C3/C6 |
| `HMAC_DOWN_JTAG` | HMAC JTAG 解锁 | ESP32-S2/S3/C3/C6 |
| `HMAC_DOWN_DIGITAL_SIGNATURE` | HMAC 数字签名 | ESP32-S2/S3/C3/C6 |
| `HMAC_UP` | HMAC 上行 | ESP32-S2/S3/C3/C6 |
| `USER` | 用户自定义 | ESP32-S2/S3/C3/C6 |
