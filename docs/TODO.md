# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 高优先级 - 协议层防御性校验加固

当前协议层对外部输入缺少边界校验，存在整数溢出和静默成功等风险。

### 代码修复

| # | 文件 | 函数 | 问题 | 修复要点 |
|---|------|------|------|----------|
| 1 | `esptool.c` | `handle_read_flash` | `bsize` 来自网络包无上限，`bsize * 2 + 2` 可整数溢出，导致 SLIP 编码越界写入；`bsize=0` 时死循环 | 拒绝 `bsize == 0` 或 `bsize > (FESP_SLIP_MAX_FRAME - 2) / 2` |
| 2 | `flash.c` | `fesp_flash_erase` | `addr + len` 整数溢出可回绕，绕过地址范围检查，在错误区域擦除 | 改为 `len > ctx->size - addr` 判断，消除溢出 |
| 3 | `esptool.c` | `handle_flash_data` / `handle_flash_defl_data` | `data_len > pkt->size - 16` 时静默成功，既不处理数据也不报错 | 条件为 false 时返回 FESP_FAIL |
| 4 | `esptool.c` | `handle_read_flash` | `addr + offset` 循环累加可能溢出（风险较低，flash.c 内部兜底） | 循环前校验 `addr <= UINT32_MAX - len` |
| 5 | `esptool.c` | `fesp_process_frame` | 缺少 `ctx` / `ctx->chip` / `ctx->flash` 的 NULL 检查 | 函数入口处添加 NULL 守卫 |
| 6 | `esptool.c` | `handle_flash_defl_data` | `defl_buf_size + data_len` 整数溢出可绕过缓冲区溢出检查，导致 memcpy 堆越界写入 | 溢出前检查 `defl_buf_size > UINT32_MAX - data_len` |
| 7 | `esptool.c` | `handle_flash_data` | `flash_offset += data_len` 累加可溢出，绕过后续边界检查 | 累加前检查 `flash_offset > UINT32_MAX - data_len` |
| 8 | `esptool.c` | `handle_flash_end` | `defl_flush_buffer` 失败后仅记日志，仍返回 FESP_OK 且状态切为 READY，Flash 数据残缺但客户端认为成功 | 失败时返回 FESP_FAIL |
| 9 | `esptool.c` | `CHECK_PKT_SIZE` 宏 | 包过小静默 `return` 不发错误响应，客户端超时而非收到明确错误码 | 过小时发送 FESP_FAIL 响应 |
| 10 | `esptool.c` | `handle_read_flash` | 先发 ACK 再分配内存，分配失败时 ACK 已发出，客户端挂起等待数据帧 | 先分配再发 ACK |
| 11 | `esptool.c` | `fesp_send_response_ex` | `fesp_slip_encode` 返回 ≤0 时静默丢弃，无错误日志 | 编码失败时记录日志 |

### 测试补充

| # | 测试文件 | 场景 | 验证点 |
|---|---------|------|--------|
| 12 | `test_esptool.c` | `handle_read_flash` bsize=0 | 返回失败，无死循环 |
| 13 | `test_esptool.c` | `handle_read_flash` bsize=0x80000000 | 返回失败，不崩溃 |
| 14 | `test_esptool.c` | `handle_flash_data` data_len 远大于 pkt->size | 返回失败，不复现静默成功 |
| 15 | `test_flash.c` | `fesp_flash_erase` addr=0xFFFFFF00, len=0x200 | 返回 false，不写入低地址 |
| 16 | `test_esptool.c` | FLASH_DATA 在 IDLE 状态下发送 | 返回失败（状态机拒绝） |
| 17 | `test_esptool.c` | MEM_DATA 带错误 checksum | 返回失败 |
| 18 | `test_esptool.c` | `handle_flash_defl_data` defl_buf_size + data_len 溢出 | 返回失败，无 memcpy 调用 |
| 19 | `test_esptool.c` | `handle_flash_end` defl_flush 失败模拟 | 返回 FESP_FAIL，非 FESP_OK |
| 20 | `test_esptool.c` | `handle_read_flash` ACK 后分配失败（注入 OOM） | 不发 ACK（需先改顺序） |

---

## 中优先级 - CLI 命令行参数扩展

通过 `WM_COPYDATA` 向已有单例发送指令，新实例发送后立即退出，实现脚本化自动化。

**机制：**

- 新实例解析命令行 → 校验参数 → 发送指令给已有实例 → 退出
- 已有实例接收指令 → 静默执行（不弹窗，不输出错误）
- 校验失败由新实例在命令行报错并退出
- `--exit` 强制最后执行

**参数列表：**

| 参数 | 动作 | 校验方 |
|------|------|--------|
| `--new [file.esp]` | 强制新实例：关闭已有实例（不保存、断开串口），有文件则打开，无文件则跳过"打开上次文件"提示 | 新实例 |
| `<file.esp>` | 打开设备文件（已有） | 新实例 |
| `--chip <chip>` | 设置芯片类型 | 新实例 |
| `--xtal-freq <freq>` | 设置晶振频率 | 新实例 |
| `--flash-size <size>` | 设置 flash 大小 | 新实例 |
| `--connect <port>` | 连接指定串口 | 新实例 |
| `--reconnect` | 连接上次使用的串口 | 新实例 |
| `--disconnect` | 断开串口 | 无 |
| `--save [file]` | 保存 .esp，不带文件名则保存到当前文件 | 新实例 |
| `--import-flash <path>` | 导入 flash | 新实例 |
| `--import-efuse <path>` | 导入 eFuse | 新实例 |
| `--export-flash <path>` | 导出 flash | 新实例 |
| `--export-efuse <path>` | 导出 eFuse | 新实例 |
| `--dump <path>` | 导出设备信息到 txt | 新实例 |
| `--encryption <none\|dev\|release>` | 设置加密状态 | 新实例 |
| `--download-mode <normal\|secure\|disabled>` | 设置下载模式 | 新实例 |
| `--save-log <path>` | 保存日志到文件 | 新实例 |
| `--exit` | 关闭已有实例（始终最后执行，无论位置） | 无 |
| `--force` | 见下方行为表 | 无 |

**参数规则：**
- `--new` 和 `--force` 必须是第一个参数，二者互斥
- `--exit` 始终最后执行，无论出现在命令行哪个位置
- 其余参数从左到右依次执行
- `--new` + `--save` 无文件名：新实例本地报错（`--new` 无关联文件，`--save` 无目标）

**`--force` 行为表（仅影响确认对话框）：**

| 场景 | --force | 无 --force |
|------|---------|-----------|
| 打开上次文件提示 | 自动打开 | 弹窗询问 |
| 断开串口确认 | 自动断开 | 弹窗询问 |
| 退出时保存确认 | 丢弃，输出警告到 stderr | 弹窗询问 |
| 覆盖文件确认 | 直接覆盖 | 弹窗询问 |

**CLI 执行模式（独立于 --force）：**

仅在 CLI 路径下生效——新实例通过 WM_COPYDATA 传递命令时，或首个实例启动时携带 CLI 参数时，已有实例设置 `g_cliExecuting` 标志，执行期间错误/警告 MessageBox 改为 TRACE 记录。新实例负责校验并向 stderr 输出错误。

已有实例的原生 GUI 操作（用户手动点击菜单、拖放文件、密钥管理等）不受此标志影响，MessageBox 正常显示。

**例外**：设备移除/断开通知（`IDS_MSG_CONN_LOST`、`IDS_MSG_DEV_REMOVED`）始终弹窗阻塞，不受 `g_cliExecuting` 影响。新实例通过 `SendMessage` 超时（等待 2 秒无响应）检测到已有实例被阻塞，向 stderr 输出错误并退出。

**使用示例：**

```bash
# 强制新实例，跳过上次文件
FakeEsptool.exe --new --connect COM10

# 强制新实例，打开指定文件
FakeEsptool.exe --new device.esp

# 自动化流程：打开设备 → 连接串口 → 等待 esptool 写入 → 导出 → 退出
FakeEsptool.exe device.esp
FakeEsptool.exe --connect COM10
# esptool 通过串口写入数据...
FakeEsptool.exe --force --export-flash flash.bin --export-efuse efuse.bin --exit

# 强制新实例 + 自定义参数
FakeEsptool.exe --new --chip ESP32-C3 --xtal-freq 40 --flash-size 4MB --connect COM10

# 覆盖保存
FakeEsptool.exe --force --save backup.esp --exit
```

**错误输出：**

- 新实例通过 `AttachConsole(ATTACH_PARENT_PROCESS)` 附加到父进程控制台
- 校验失败时 `fprintf(stderr, ...)` 输出错误信息

**实现内容：**

1. `main.c`: 解析命令行参数，区分文件路径和 `--` 开头的选项
2. `main.c`: 检测到 CLI 选项时调用 `AttachConsole` + 重定向 stderr
3. `main.c`: 单例模式下发送 `WM_COPYDATA` 携带命令类型和参数
4. `main.c`: 解析 `--force` 标志，传递给各处理函数
5. `main.c`: `--exit` 标志强制排在所有命令之后执行
6. `app_commands.c`: 各导出/导入/保存函数增加 force 参数，跳过对话框
7. `app_commands.c`: `--dump` 导出设备信息到 txt（复用 Dump Device As 逻辑）
8. `app_commands.c`: `--encryption` / `--download-mode` 切换 eFuse 位
9. `app_commands.c`: `--save-log` 保存日志到文件
10. `main.c`: CLI 校验函数（文件存在性、串口可用性等）
11. `docs/REQUIREMENTS.md`: 补充命令行参数说明
12. `docs/DEVELOPMENT.md`: 记录 CLI 方案缺陷——WM_COPYDATA 单向通信，已有实例执行失败无法回传错误；导出类操作通过发送后检查输出文件间接验证

---

## 远期规划 - 新芯片支持

支持 esptool 官方新增的 ESP 芯片（部分特性待完善）。

| 芯片 | 特性 | eFuse基址 | SPI基址 | 晶振 | IMAGE_CHIP_ID | 继承 |
|------|------|-----------|---------|------|---------------|------|
| ESP32-H21 | BT5+802.15.4, RISC-V单核96MHz | 0x600B4000 | 0x60003000 | 32MHz | 25 | ESP32H2ROM |
| ESP32-H4 | BT5+802.15.4, RISC-V双核96MHz | 0x600B1800 | 0x60099000 | 32MHz | 28 | ESP32C3ROM |
| ESP32-E22 | WiFi 6E+BT5.4, 双核500MHz | 0xC4008000 | 0xC3003000 | 动态检测 | 31 | ESP32ROM |

**实现内容：**
- chip.h: 添加芯片枚举
- chip.c: 实现各芯片初始化函数
- chip.c: 补充 eFuse 布局、MAC 地址偏移、SPI 寄存器偏移
- chip.c: 实现 fesp_chip_get_boot_message 启动日志
- main.c: Device Properties 对话框添加新芯片选项
- REQUIREMENTS.md、DEVELOPMENT.md: 同步更新芯片支持列表

**注意事项：**
- ESP32-E22 的 eFuse 字段尚未完全分配（DIS_DOWNLOAD_MANUAL_ENCRYPT、SPI_BOOT_CRYPT_CNT 未定义）
- ESP32-E22 的 UART_DATE_REG 地址不同于其他芯片（0xC310208C）
- ESP32-E22 的 GPIO_STRAP_REG 在 0xC310D000
- ESP32-H21 无 stub loader
- ESP32-E22 无 stub loader
- ESP32-H4 支持 EUI64 MAC 格式

---
