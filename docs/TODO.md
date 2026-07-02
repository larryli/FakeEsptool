# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 高优先级 - .esp 文件格式升级 v2

将 .esp 文件格式升级为 v2，使 eFuse 数据仅包含块读数据，与 QEMU/esp-emulator 格式兼容。

**背景：**

当前 .esp 文件（v1）将 eFuse 存储为扁平字节数组，包含完整的寄存器地址空间（288/512 字节），其中混入了 PGM 寄存器、CLK/CONF/CMD 控制寄存器等易失性内容。这些内容在真实硬件上掉电不保留，协议也假设 reset 后从干净状态开始，保存它们毫无意义。

QEMU 和 esp-emulator 只使用实际的块读数据（ROM 部分），控制寄存器由模拟器自身管理。

**各芯片 eFuse 块布局（基于 espefuse mem_definition.py）：**

ESP32 (Xtensa) - 124 字节：

| Block | 偏移 | 字数 | 说明 |
|-------|------|------|------|
| BLOCK0 | 0x000 | 7 | WR_DIS + 控制位 |
| BLOCK1 | 0x038 | 8 | Flash 加密密钥 |
| BLOCK2 | 0x058 | 8 | 安全启动密钥 |
| BLOCK3 | 0x078 | 8 | 用户数据 |

ESP32-C2 - 52 字节：

| Block | 偏移 | 字数 | 说明 |
|-------|------|------|------|
| BLOCK0 | 0x02C | 2 | WR_DIS + 控制位 |
| BLOCK1 | 0x034 | 3 | MAC + 系统数据 |
| BLOCK2 | 0x040 | 8 | 系统数据 |
| KEY0 | 0x060 | 8 | 密钥 |

ESP32-S2/S3/C3/C5/C6/C61/H2/P4/S31 (RISC-V 新架构) - 336 字节：

| Block | 偏移 | 字数 | 说明 |
|-------|------|------|------|
| BLOCK0 | 0x02C | 6 | WR_DIS + REPEAT_DATA |
| BLOCK1/MAC | 0x044 | 6 | MAC + SPI + SYS |
| BLOCK2/SYS_DATA | 0x05C | 8 | 系统数据 |
| BLOCK3/USR_DATA | 0x07C | 8 | 用户数据 |
| KEY0-5 | 0x09C-0x13C | 8×6 | 密钥块 |
| SYS_DATA2 | 0x15C | 8 | 系统数据2 |

ESP32-S31 - 296 字节（无 KEY5）：

| Block | 偏移 | 字数 | 说明 |
|-------|------|------|------|
| BLOCK0 | 0x02C | 9 | WR_DIS + REPEAT_DATA |
| BLOCK1/MAC | 0x050 | 6 | MAC + SPI + SYS |
| BLOCK2/SYS_DATA | 0x068 | 8 | 系统数据 |
| BLOCK3/USR_DATA | 0x088 | 8 | 用户数据 |
| KEY0-4 | 0x0A8-0x128 | 8×5 | 密钥块 |
| SYS_DATA2 | 0x148 | 8 | 系统数据2 |

**变更内容：**

1. `device_file.h`: `DEVICE_FILE_VERSION` 改为 2
2. `device_file.c`: 新增 `efuse_block_size` 表，按芯片返回块数据大小
3. `device_file.c`: Save 时只写入块读数据（从 efuse 数组中按偏移提取）
4. `device_file.c`: Load v2 时直接读取块数据写入 efuse 数组对应偏移
5. `device_file.c`: Load v1 兼容——检测到 version=1 时**直接忽略** efuse 易失数据（PGM 寄存器、CLK/CONF/CMD 控制寄存器），只提取块读数据
6. `efuse.c`/`app_protocol.c`: 串口连接（`Main_OnConnect`）和设备收到 reset 信号（`OnEsptoolSignal` 进入下载模式）时，清除 efuse 数组中的易失性区域（PGM 数据、控制寄存器偏移位置归零）
7. `docs/REQUIREMENTS.md`: 更新设备文件格式说明
8. `docs/DEVELOPMENT.md`: 记录格式变更和 QEMU 兼容性

---

## 高优先级 - eFuse 导入导出（QEMU 兼容）

为除 ESP8266 外的所有芯片增加 eFuse 块数据的导入导出功能，格式与 QEMU/esp-emulator 兼容。

**菜单行为：**

| 芯片 | Import eFuse | Export eFuse |
|------|-------------|-------------|
| ESP8266 | 禁用（灰色） | 禁用（灰色） |
| 其他所有芯片 | 启用 | 启用 |

**功能描述：**

- **Export eFuse**：从 .esp 设备状态中提取块读数据，保存为独立的 .bin 文件（QEMU 兼容格式）
- **Import eFuse**：从 QEMU 兼容的 .bin 文件导入块读数据，写入 efuse 数组对应偏移
- 文件大小校验：导入时检查文件大小是否与当前芯片的块数据大小匹配
- 默认文件名：`<芯片名>_efuse.bin`（如 `ESP32-C3_efuse.bin`）

**实现内容：**

1. `app_commands.c`: 添加 `IDM_EXPORT_EFUSE` / `IDM_IMPORT_EFUSE` 菜单命令
2. `app_commands.c`: 添加 `Main_OnEfuseExport` / `Main_OnEfuseImport` 处理函数
3. `app_commands.c`: ESP8266 时禁用菜单项（灰色）
4. `efuse.c`: 新增 `fesp_efuse_export_blocks` — 提取块读数据到缓冲区
5. `efuse.c`: 新增 `fesp_efuse_import_blocks` — 从缓冲区写入块数据到 efuse 数组
6. `resource.rc` / 菜单定义：添加菜单项
7. `docs/REQUIREMENTS.md`: 添加菜单说明
8. `docs/DEVELOPMENT.md`: 记录功能和 QEMU 兼容性
9. `docs/DEVELOPMENT.md`: 说明 espefuse `--virt` 模式的 eFuse 格式（完整寄存器地址空间，512 字节）与 QEMU/esp-emulator 块数据格式的区别
10. `tools/efuse_convert.py`: 实现两种 eFuse 格式的相互转换脚本
    - QEMU 块数据格式（124/336 字节） ↔ espefuse --virt 格式（512 字节完整寄存器空间）
    - 按芯片类型自动确定偏移映射
    - 支持命令行调用：`efuse_convert.py --chip esp32c3 --from qemu --to virt input.bin output.bin`

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
