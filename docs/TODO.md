# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 1. FLASH_DEFL_DATA DEFLATE 解压

**优先级**：高  
**复杂度**：高  
**影响范围**：压缩模式烧录功能

### 问题描述

当前 `HandleFlashDeflData` 将压缩数据直接写入 Flash，未进行 DEFLATE 解压。真实设备（ROM bootloader 和 stub）会先解压再写入。

esptool 客户端使用 pako（JavaScript）或 zlib（C/Python）的 DEFLATE 算法（level=9）压缩固件，然后分块发送。设备端需要解压后才能正确写入 Flash。

### 当前行为

```
客户端发送压缩数据 → 设备直接写入 Flash → Flash 内容为压缩数据（错误）
```

### 期望行为

```
客户端发送压缩数据 → 设备解压 → 写入解压后的数据到 Flash → Flash 内容正确
```

### 建议方案

1. **集成 miniz 库**（单头文件实现，约 280KB）
   - 源码：https://github.com/richgel999/miniz
   - 仅需 `miniz.h` 和 `miniz.c`，放入 `src/utils/` 目录
   - CMakeLists.txt 添加 `src/utils/miniz.c`

2. **修改 `HandleFlashDeflData`**
   ```c
   // 伪代码
   static void HandleFlashDeflData(ESPTOOL_CTX *ctx, const ESP_PACKET *pkt)
   {
       // 1. 解析 data_len 和 seq
       // 2. 验证 checksum
       // 3. 解压数据
       BYTE *decompressed = malloc(decompress_size);
       tinfl_decompress_mem_to_mem(decompressed, decompress_size, 
                                   payload, data_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
       // 4. 写入解压后的数据
       Flash_Write(&ctx->flash, ctx->flash_offset, decompressed, actual_size);
       // 5. 更新偏移
       ctx->flash_offset += actual_size;
       free(decompressed);
   }
   ```

3. **处理边界情况**
   - 解压缓冲区大小：根据 `FLASH_DEFL_BEGIN` 的 `uncompressed_size` 分配
   - 解压失败：返回 `ESP_FAIL` 状态
   - 内存分配失败：返回 `ESP_FAIL` 状态

### 参考

- esptool-js 实现：`src/esploader.ts` line 927-943
- PROTOCOL.md §3.16 DEFL 压缩算法说明

---

## 2. SPI 寄存器偏移按芯片族区分

**优先级**：中  
**复杂度**：中  
**影响范围**：SPI Flash ID 读取（RDID 0x9F 命令）

### 问题描述

当前 `chip.h` 定义了一组 SPI 寄存器偏移（适用于 ESP32-S2/S3/C2/C3/C6），但 ESP32 和 ESP8266 使用不同的寄存器布局。

| 寄存器 | ESP32-S2/S3/C2/C3/C6 | ESP32 | ESP8266 |
|--------|----------------------|-------|---------|
| SPI_USR | 0x18 | 0x1C | 0x1C |
| SPI_USR1 | 0x1C | 0x20 | 0x20 |
| SPI_USR2 | 0x20 | 0x24 | 0x24 |
| SPI_W0 | 0x58 | 0x80 | 0x40 |
| SPI_MOSI_DLEN | 0x24 | 0x28 | N/A |
| SPI_MISO_DLEN | 0x28 | 0x2C | N/A |

### 当前行为

所有芯片使用同一组偏移，导致 ESP32 和 ESP8266 的 SPI 寄存器操作使用错误偏移。

### 期望行为

根据芯片类型选择正确的 SPI 寄存器偏移。

### 建议方案

1. **定义芯片族 SPI 偏移结构**
   ```c
   typedef struct {
       BYTE usr;
       BYTE usr1;
       BYTE usr2;
       BYTE w0;
       BYTE mosi_dlen;
       BYTE miso_dlen;
   } SPI_OFFSETS;
   ```

2. **在 `CHIP_CTX` 中添加 SPI 偏移指针**
   ```c
   typedef struct {
       ...
       const SPI_OFFSETS *spi_offs;  /* 指向芯片族的 SPI 偏移 */
       ...
   } CHIP_CTX;
   ```

3. **在 `Chip_Init` 中根据芯片类型设置偏移**
   ```c
   static const SPI_OFFSETS esp32s2_offsets = { 0x18, 0x1C, 0x20, 0x58, 0x24, 0x28 };
   static const SPI_OFFSETS esp32_offsets   = { 0x1C, 0x20, 0x24, 0x80, 0x28, 0x2C };
   static const SPI_OFFSETS esp8266_offsets = { 0x1C, 0x20, 0x24, 0x40, 0x00, 0x00 };
   ```

4. **修改 `Chip_ReadReg` 和 `Chip_WriteReg` 使用偏移指针**

### 参考

- PROTOCOL.md 附录 E SPI 寄存器
- esptool-js：`src/targets/esp32.ts`、`src/targets/esp8266.ts`

---

## 3. eFuse 写入支持所有芯片基地址

**优先级**：低  
**复杂度**：低  
**影响范围**：eFuse 写入命令

### 问题描述

当前 `Chip_WriteReg` 仅处理 ESP32 的 eFuse 地址范围（`0x3FF00000`）。其他芯片使用不同的 eFuse 基地址：

| 芯片 | eFuse 基地址 |
|------|-------------|
| ESP32 | 0x3FF00000 |
| ESP32-S2 | 0x3F41A000 |
| ESP32-S3 | 0x60007000 |
| ESP32-C2 | 0x60008800 |
| ESP32-C3 | 0x60008800 |
| ESP32-C6 | 0x600B0800 |

### 当前行为

对非 ESP32 芯片的 eFuse 写入被静默忽略。

### 期望行为

所有芯片的 eFuse 写入都能正确处理。

### 建议方案

在 `Chip_WriteReg` 中添加各芯片的 eFuse 地址范围处理：

```c
BOOL Chip_WriteReg(CHIP_CTX *ctx, DWORD addr, DWORD val)
{
    /* ESP32 EFUSE: 0x3FF00000 */
    if (addr >= 0x3FF00000 && addr < 0x3FF00000 + ctx->efuse_size) {
        // 现有代码...
    }
    
    /* ESP32-S2 EFUSE: 0x3F41A000 */
    if (addr >= 0x3F41A000 && addr < 0x3F41A000 + ctx->efuse_size) {
        int offset = (int)(addr - 0x3F41A000);
        ctx->efuse[offset] |= (BYTE)(val & 0xFF);
        // ...
    }
    
    /* ESP32-S3 EFUSE: 0x60007000 */
    if (addr >= 0x60007000 && addr < 0x60007000 + ctx->efuse_size) {
        // ...
    }
    
    /* ESP32-C2/C3 EFUSE: 0x60008800 */
    if (addr >= 0x60008800 && addr < 0x60008800 + ctx->efuse_size) {
        // ...
    }
    
    /* ESP32-C6 EFUSE: 0x600B0800 */
    if (addr >= 0x600B0800 && addr < 0x600B0800 + ctx->efuse_size) {
        // ...
    }
    
    // ...
}
```

### 参考

- `Chip_ReadReg` 已实现所有芯片的 eFuse 读取
- PROTOCOL.md 附录 G eFuse 基地址汇总

---

## 4. 命令状态机

**优先级**：低  
**复杂度**：中  
**影响范围**：协议健壮性

### 问题描述

当前实现按任何顺序处理命令，不验证命令序列。真实设备有隐式状态机：

```
IDLE → SYNC_DONE → CHIP_DETECTED → STUB_LOADING → STUB_READY
```

例如，`FLASH_DATA` 在 `FLASH_BEGIN` 之前发送应该被拒绝。

### 当前行为

任何命令在任何时候都能被处理，不会报错。

### 期望行为

按状态机验证命令序列，非法序列返回错误。

### 建议方案

1. **定义协议状态枚举**
   ```c
   typedef enum {
       ESP_STATE_IDLE,
       ESP_STATE_SYNCED,
       ESP_STATE_READY,      /* 已同步，可接受寄存器/内存命令 */
       ESP_STATE_FLASH_WRITING, /* FLASH_BEGIN 已发送 */
       ESP_STATE_MEM_WRITING,   /* MEM_BEGIN 已发送 */
   } ESP_STATE;
   ```

2. **在 `ESPTOOL_CTX` 中添加状态字段**
   ```c
   typedef struct {
       ...
       ESP_STATE state;
       ...
   } ESPTOOL_CTX;
   ```

3. **在命令处理前验证状态**
   ```c
   case ESP_CMD_FLASH_DATA:
       if (ctx->state != ESP_STATE_FLASH_WRITING) {
           Esptool_SendResponse(ctx, ESP_CMD_FLASH_DATA, pkt.value, ESP_FAIL, NULL, 4);
           return FALSE;
       }
       HandleFlashData(ctx, &pkt);
       break;
   ```

4. **状态转换**
   - `SYNC` → `ESP_STATE_SYNCED`
   - `FLASH_BEGIN` → `ESP_STATE_FLASH_WRITING`
   - `FLASH_END` → `ESP_STATE_READY`
   - `MEM_BEGIN` → `ESP_STATE_MEM_WRITING`
   - `MEM_END` → `ESP_STATE_READY`

### 参考

- PROTOCOL.md §H 状态机定义
- esptool-js：`src/esploader.ts` 中的状态检查
