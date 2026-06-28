# FakeEsptool - API 参考

本文档描述 FakeEsptool 各模块的公共 API 接口。

---

## esptool.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `fesp_packet_t` | 协议数据包（约 32KB） |
| `fesp_ctx_t` | 协议上下文（包含 fesp_packet_t 预分配缓冲区） |

**fesp_packet_t 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `direction` | BYTE | 请求 (0x00) 或响应 (0x01) |
| `command` | BYTE | 命令码 |
| `size` | WORD | 数据载荷大小 |
| `value` | DWORD | 命令相关值 |
| `data[fesp_packet_t_DATA_MAX]` | BYTE | 数据载荷 |

**fesp_ctx_t 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `slip` | fesp_slip_ctx_t | SLIP 解码器上下文 |
| `chip` | fesp_chip_ctx_t* | 指向设备芯片数据（不拥有） |
| `flash` | fesp_flash_ctx_t* | 指向设备 Flash 数据（不拥有） |
| `pkt` | fesp_packet_t | 预分配数据包缓冲区（避免栈溢出） |
| `state` | ESP_STATE | 协议状态机 |
| `synced` | BOOL | SYNC 握手完成标志 |
| `stub_mode` | BOOL | Stub 运行标志 |
| `hNotify` | HWND | UI 通知窗口 |
| `onModified` | ESP_MODIFIED_CB | 设备修改回调 |
| `onWrite` | ESP_WRITE_CB | 串口写回调 |
| `onBaudRate` | ESP_BAUDRATE_CB | 波特率修改回调 |
| `flash_offset` | DWORD | 当前 Flash 写入偏移 |
| `flash_seq` | DWORD | 当前 Flash 写入序列号 |
| `last_read_val` | DWORD | 上次 READ_REG 的值 |
| `flash_uncompressed_size` | DWORD | DEFLATE 解压大小 |
| `defl_buf` | BYTE* | 压缩数据积累缓冲区 |
| `defl_buf_size` | DWORD | 当前积累数据大小 |
| `defl_buf_cap` | DWORD | 缓冲区容量 |
| `defl_offset` | DWORD | 当前 deflate 会话的 Flash 偏移 |
| `defl_unc_size` | DWORD | 当前 deflate 会话的未压缩大小 |

**ESP_STATE 枚举：**

| 值 | 说明 |
|------|------|
| `ESP_STATE_IDLE` | 初始状态，等待 SYNC |
| `ESP_STATE_SYNCED` | 已同步，等待芯片检测 |
| `ESP_STATE_READY` | 芯片已检测，可接受命令 |
| `ESP_STATE_FLASH_WRITING` | FLASH_BEGIN 已发送，等待数据 |
| `ESP_STATE_MEM_WRITING` | MEM_BEGIN 已发送，等待数据 |

**状态转换规则：**

| 命令 | 转换 |
|------|------|
| SYNC | → SYNCED |
| READ_REG (0x40001000) | SYNCED → READY |
| FLASH_BEGIN / FLASH_DEFL_BEGIN | → FLASH_WRITING |
| FLASH_END / FLASH_DEFL_END | → READY |
| MEM_BEGIN | → MEM_WRITING |
| MEM_END | → READY |
| RUN_USER_CODE | → IDLE |

**函数：**

| 函数 | 说明 |
|------|------|
| `fesp_init(ctx, chip, flash)` | 初始化上下文，绑定设备数据指针 |
| `fesp_reset_state(ctx)` | 重置协议状态（进入下载模式时调用） |
| `FEsptoolInit(hWnd, serial)` | 初始化 HAL（传入窗口句柄和串口上下文） |
| `fesp_feed(ctx, data, len)` | 喂入串口数据 |
| `fesp_process_frame(ctx, frame, frame_len)` | 处理一帧数据 |
| `fesp_send_response(ctx, cmd, req_val, status, data, len)` | 发送响应（4字节状态） |
| `fesp_send_response_ex(ctx, cmd, req_val, status, status_len, data, len)` | 发送响应（可配置状态长度） |
| `fesp_calc_checksum(data, len)` | 计算校验和 |

**注意：** `SendResponseEx` 的 `status` 参数仅用于日志记录（TRACE_PROTO 和 Serial_PostLogF），不包含在响应包中。`status_len` 参数决定响应 Data 字段的总长度（`data_len` + 状态码），当 `data=NULL` 且 `data_len>0` 时，函数自动填充零字节（表示成功）。`SendResponse` 是 `SendResponseEx` 的便捷封装，固定使用 4 字节状态长度。

---

## chip.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `fesp_spi_offsets_t` | SPI 寄存器偏移（按芯片族区分） |
| `fesp_chip_ctx_t` | 芯片上下文（包含 fesp_spi_offsets_t 指针） |

**fesp_spi_offsets_t 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `usr` | BYTE | SPI_USR 偏移 |
| `usr1` | BYTE | SPI_USR1 偏移 |
| `usr2` | BYTE | SPI_USR2 偏移 |
| `w0` | BYTE | SPI_W0 偏移 |
| `mosi_dlen` | BYTE | SPI_MOSI_DLEN 偏移（0=不支持） |
| `miso_dlen` | BYTE | SPI_MISO_DLEN 偏移（0=不支持） |

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `FESP_CHIP_DETECT_REG` | `0x40001000` | 芯片检测魔数寄存器地址（用于 esptool 自动识别） |

**fesp_chip_ctx_t eFuse 控制器字段（用于 C2/C3/C6 烧录模拟）：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `pgm_data[8]` | DWORD[] | PGM_DATA0-7 暂存区（烧录数据暂存） |
| `efuse_conf_ofs` | DWORD | CONF_REG 相对 EFUSE_BASE 的偏移（0 = 无控制器） |
| `efuse_cmd_ofs` | DWORD | CMD_REG 相对 EFUSE_BASE 的偏移 |

**函数：**

| 函数 | 说明 |
|------|------|
| `fesp_chip_init(ctx, type)` | 初始化芯片 |
| `fesp_chip_close(ctx)` | 释放芯片 |
| `fesp_chip_get_name(ctx)` | 获取芯片名称 |
| `fesp_chip_set_mac(ctx, mac)` | 设置MAC地址 |
| `fesp_chip_get_mac(ctx)` | 获取MAC地址 |
| `fesp_chip_read_reg(ctx, addr)` | 读取寄存器（含 eFuse 值存储读取） |
| `fesp_chip_write_reg(ctx, addr, val)` | 写入寄存器（含 eFuse 控制器烧录模拟） |
| `fesp_chip_set_flash_size(ctx, size)` | 设置Flash大小 |
| `fesp_chip_get_flash_size(ctx)` | 获取Flash大小 |
| `fesp_chip_get_chip_id(ctx)` | 获取芯片ID |
| `fesp_chip_get_efuse(ctx)` | 获取efuse数据 |
| `fesp_chip_get_efuse_size(ctx)` | 获取efuse大小 |
| `fesp_chip_get_boot_baud_rate(ctx)` | 获取启动日志波特率 |
| `fesp_chip_get_boot_message(ctx, download_mode, reset_cause)` | 获取启动日志文本 |
| `fesp_efuse_is_flash_encryption_enabled(ctx)` | 检查 Flash 加密是否启用 |
| `fesp_efuse_is_download_encrypt_disabled(ctx)` | 检查下载模式加密是否禁用（量产模式） |
| `fesp_efuse_is_download_mode_disabled(ctx)` | 检查下载模式是否禁用 |
| `fesp_efuse_is_secure_download_enabled(ctx)` | 检查安全下载模式是否启用 |
| `fesp_efuse_is_secure_boot_enabled(ctx)` | 检查安全启动是否启用 |
| `fesp_efuse_is_jtag_disabled(ctx)` | 检查 JTAG 是否禁用（DIS_PAD_JTAG） |
| `fesp_efuse_get_jtag_disabled_count(ctx)` | 获取已禁用的 JTAG 接口数 |
| `fesp_efuse_get_jtag_total_count(ctx)` | 获取 JTAG 接口总数 |
| `fesp_efuse_get_flash_crypt_cnt(ctx)` | 获取 SPI_BOOT_CRYPT_CNT 原始值 |
| `fesp_efuse_get_dl_encrypt_disabled(ctx)` | 获取 DIS_DOWNLOAD_MANUAL_ENCRYPT 原始值 |
| `fesp_efuse_get_dl_mode_disabled(ctx)` | 获取 DIS_DOWNLOAD_MODE 原始值 |
| `fesp_efuse_get_secure_boot_flag(ctx)` | 获取安全启动 eFuse 原始值 |
| `fesp_efuse_get_jtag_flag(ctx)` | 获取 DIS_PAD_JTAG 原始值 |
| `fesp_efuse_get_soft_jtag_flag(ctx)` | 获取 SOFT_DIS_JTAG 原始值 |
| `fesp_efuse_get_usb_jtag_flag(ctx)` | 获取 DIS_USB_JTAG 原始值 |
| `fesp_efuse_get_key_purpose(ctx, block)` | 获取密钥块用途 |
| `fesp_efuse_get_encryption_key_offset(ctx, key_len)` | 获取加密密钥 eFuse 偏移和长度 |
| `fesp_efuse_set_flash_encryption(ctx, mode)` | **GUI 层**：设置加密状态（0=无, 1=开发, 2=量产） |
| `fesp_efuse_set_download_mode(ctx, mode)` | **GUI 层**：设置下载模式（0=正常, 1=安全, 2=禁用） |

**fesp_chip_write_reg eFuse 控制器行为：**
- 对于有控制器的芯片（C2/C3/C6，`efuse_conf_ofs != 0`）：
  - 写入 PGM_DATA 范围（`EFUSE_BASE+0x00..+0x1F`）→ 暂存到 `pgm_data[]`
  - 写入 CONF_REG → 记录解锁码
  - 写入 CMD_REG 且 bit1 置位 → 触发烧录：根据 block 编号将 `pgm_data[]` OR 写入 eFuse 数组的 block 位置
  - 其他寄存器写入 → 忽略
- 对于无控制器的芯片（ESP32/S2/S3）：直接 OR 写入 eFuse 数组

---

## flash.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `fesp_flash_ctx_t` | Flash 存储上下文 |

**fesp_flash_ctx_t 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `data` | BYTE* | Flash 数据缓冲区 |
| `size` | DWORD | Flash 大小（字节） |

**函数：**

| 函数 | 说明 |
|------|------|
| `fesp_flash_init(ctx, size)` | 初始化 Flash |
| `fesp_flash_close(ctx)` | 释放 Flash |
| `fesp_flash_read(ctx, addr, buf, len)` | 读取数据 |
| `fesp_flash_write(ctx, addr, data, len)` | 写入数据（AND 操作，模拟真实 Flash 行为） |
| `fesp_flash_erase(ctx, addr, len)` | 擦除区域（自动 4KB 扇区对齐，设为 0xFF） |
| `fesp_flash_erase_all(ctx)` | 擦除全部 |
| `fesp_flash_calc_md5(ctx, addr, len, md5)` | 计算 MD5 |

**fesp_flash_write 行为说明：**
- 真实 Flash 存储器只能将位从 1 改为 0，不能从 0 改为 1
- 要将 0 改为 1，必须先擦除扇区（设为 0xFF）
- 此函数执行：`flash[i] &= data[i]`

---

## deflate.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `DEFLATE_HUFF` | 霍夫曼编码表 |
| `DEFLATE_CTX` | 解压器上下文 |

**DEFLATE_HUFF 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `counts` | WORD* | 每个长度的编码数量 |
| `symbols` | WORD* | 按编码排序的符号表 |
| `max_length` | int | 最大编码长度 |

**DEFLATE_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `in_buf` | const BYTE* | 输入缓冲区（压缩数据） |
| `in_len` | size_t | 输入数据长度 |
| `in_pos` | size_t | 当前输入位置 |
| `out_buf` | BYTE* | 输出缓冲区（解压数据） |
| `out_len` | size_t | 输出缓冲区大小 |
| `out_pos` | size_t | 当前输出位置 |
| `bit_buf` | DWORD | 位缓冲区 |
| `bit_count` | int | 位缓冲区中的有效位数 |
| `lit_huff` | DEFLATE_HUFF | 字面量/长度霍夫曼编码 |
| `dist_huff` | DEFLATE_HUFF | 距离霍夫曼编码 |

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `DEFLATE_OK` | 0 | 成功 |
| `DEFLATE_ERROR` | -1 | 通用错误 |
| `DEFLATE_BAD_INPUT` | -2 | 输入数据无效 |
| `DEFLATE_NO_MEMORY` | -3 | 内存分配失败 |

**函数：**

| 函数 | 说明 |
|------|------|
| `Deflate_Init(ctx, in_buf, in_len, out_buf, out_len)` | 初始化解压器上下文 |
| `Deflate_Decompress(ctx)` | 执行 DEFLATE 解压 |

**使用示例：**

```c
#include "utils/deflate.h"

BYTE compressed[] = { /* ... */ };
BYTE decompressed[4096];
DEFLATE_CTX ctx;

Deflate_Init(&ctx, compressed, sizeof(compressed), decompressed, sizeof(decompressed));
int ret = Deflate_Decompress(&ctx);
if (ret == DEFLATE_OK) {
    // ctx.out_pos 包含解压后的数据长度
    // decompressed[0..ctx.out_pos-1] 包含解压后的数据
}
```

---

## encrypt.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `ENCRYPT_CTX` | 加密上下文 |

**ENCRYPT_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `key` | BYTE[64] | XTS 密钥（32 或 64 字节） |
| `key_len` | int | 密钥长度（32 或 64 字节） |
| `flash_addr` | DWORD | Flash 地址（用于 XTS tweak 计算） |

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `ENCRYPT_OK` | 0 | 成功 |
| `ENCRYPT_ERROR` | -1 | 通用错误 |
| `ENCRYPT_BAD_INPUT` | -2 | 输入数据无效 |
| `ENCRYPT_KEY_LEN_256` | 32 | 256-bit 密钥（XTS-AES-128） |
| `ENCRYPT_KEY_LEN_512` | 64 | 512-bit 密钥（XTS-AES-256） |
| `ENCRYPT_BLOCK_SIZE` | 128 | AES-XTS 块大小（1024 位） |

**函数：**

| 函数 | 说明 |
|------|------|
| `Encrypt_Init(ctx, key, key_len, flash_addr)` | 初始化加密上下文 |
| `Encrypt_Data(ctx, in_buf, out_buf, len)` | 加密数据 |
| `Decrypt_Data(ctx, in_buf, out_buf, len)` | 解密数据 |

**使用示例：**

```c
#include "utils/encrypt.h"

BYTE key[32] = { /* ... */ };
BYTE plaintext[4096];
BYTE ciphertext[4096];
ENCRYPT_CTX ctx;

Encrypt_Init(&ctx, key, 32, 0x10000);
Encrypt_Data(&ctx, plaintext, ciphertext, sizeof(plaintext));

// 解密
Decrypt_Data(&ctx, ciphertext, plaintext, sizeof(ciphertext));
```

---

## slip.h

| 函数 | 说明 |
|------|------|
| `fesp_slip_init(ctx)` | 初始化解码器 |
| `fesp_slip_put_byte(ctx, b)` | 喂入字节 |
| `fesp_slip_is_complete(ctx)` | 检查帧完成 |
| `fesp_slip_get_payload(ctx)` | 获取载荷 |
| `fesp_slip_get_length(ctx)` | 获取长度 |
| `fesp_slip_reset(ctx)` | 重置状态 |
| `fesp_slip_encode(data, len, out, max)` | 编码一帧 |

---

## device.h

**数据结构：**

| 结构体 | 说明 |
|--------|------|
| `DEVICE_CTX` | 设备上下文 |

**DEVICE_CTX 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `chip` | fesp_chip_ctx_t | 芯片特性（类型、MAC、eFuse、晶振频率） |
| `flash` | fesp_flash_ctx_t | Flash 存储（数据缓冲区、大小） |
| `filename` | WCHAR[MAX_PATH] | 当前文件路径 |
| `modified` | BOOL | 数据修改标记 |

**常量：**

| 常量 | 值 | 说明 |
|------|-----|------|
| `DEVICE_MAGIC` | `0x45535000` | 文件魔数 ("ESP\0") |
| `DEVICE_VERSION` | `1` | 文件格式版本 |

**函数：**

| 函数 | 说明 |
|------|------|
| `Device_Init(ctx, chipType, flashSize, mac)` | 初始化设备 |
| `Device_Close(ctx)` | 释放设备资源 |
| `Device_Save(ctx, filename)` | 保存设备到 .esp 文件 |
| `Device_Load(ctx, filename)` | 从 .esp 文件加载设备 |
| `Device_IsModified(ctx)` | 检查是否已修改 |
| `Device_SetModified(ctx, modified)` | 设置修改标记 |
| `Device_GetFilename(ctx)` | 获取当前文件路径 |

---

## serial.h

| 函数 | 说明 |
|------|------|
| `Serial_EnumPorts(hCombo)` | 枚举可用串口到下拉框 |
| `Serial_Open(ctx, port, hNotify)` | 打开串口 |
| `Serial_Close(ctx)` | 关闭串口 |
| `Serial_IsOpen(ctx)` | 检查状态 |
| `Serial_WriteData(ctx, data, len, hNotify)` | 写入数据 |
| `Serial_SetReceiveCallback(ctx, cb)` | 设置接收回调 |
| `Serial_SetSignalCallback(ctx, cb)` | 设置信号回调 |
| `Serial_SetDtr(ctx, state)` | 设置DTR |
| `Serial_SetRts(ctx, state)` | 设置RTS |
| `Serial_SetBaudRate(ctx, baudRate)` | 修改波特率 |
| `Serial_SetDataBits(ctx, bits)` | 修改数据位 |
| `Serial_SetParity(ctx, parity)` | 修改校验 |
| `Serial_SetStopBits(ctx, bits)` | 修改停止位 |
| `Serial_GetConfig(ctx, ...)` | 读取配置 |
| `Serial_GetRxBytes(ctx)` | 获取接收字节数 |
| `Serial_GetTxBytes(ctx)` | 获取发送字节数 |
| `Serial_GetPortName(index, portName, maxLen)` | 获取端口名（返回 BOOL） |
| `Serial_PostLog(hNotify, tag, text)` | 发送日志 |
| `Serial_PostLogF(hNotify, tag, fmt, ...)` | 格式化日志 |

**Listener 线程注意事项：**
- `Listener_Proc` 中不要使用过严的读取条件（如 `cbInQue < READ_BUFFER_SIZE`），应使用 `min(cbInQue, READ_BUFFER_SIZE)` 安全截断
- 避免在 listener 线程中同步调用 UI 函数（如 `SetWindowTextW`），应使用 `PostMessage` 异步通知
- stub 模式的 `FLASH_DEFL_DATA` 包经 SLIP 编码后约 16500 字节，`READ_BUFFER_SIZE` (32KB) 足以一次读取

**Serial_SetBaudRate 注意事项：**
- `FlushFileBuffers` 只保证数据到达 USB 串口芯片的内部 FIFO，不保证 FIFO 已物理发出
- 切换波特率前需等待 FIFO 排空，否则 FIFO 中剩余字节以错误波特率发送导致数据损坏
- 当前实现按 `256 * 10 / BaudRate` 计算延迟（256 字节 FIFO + 起止位），com0com 等虚拟串口不受影响

---

## app_commands.h (状态栏与菜单)

**状态栏函数：**

| 函数 | 说明 |
|------|------|
| `UpdateStatusBar()` | 更新状态栏 6 栏内容和 Tooltip |
| `CreateStatusTooltip(hParent)` | 创建状态栏 Tooltip 控件（TTS_BALLOON，父窗口 NULL） |

**菜单命令函数：**

| 函数 | 说明 |
|------|------|
| `Main_CmdEncryptState(hWnd, state)` | 切换加密状态（0=无, 1=开发, 2=量产），修改 eFuse |
| `Main_CmdDownloadMode(hWnd, mode)` | 切换下载模式（0=正常, 1=安全, 2=禁用），修改 eFuse |

**状态栏 Tooltip 实现：**
- 使用独立 Tooltip 控件（非 `SBARS_TOOLTIPS`）
- `TTF_SUBRECT` (0x0010) 按栏位矩形命中测试
- `TTM_DELTOOL` + `TTM_ADDTOOL` 更新文本
- 父窗口设为 NULL 避免裁剪问题
