# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

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
