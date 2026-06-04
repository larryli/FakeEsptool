# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 1. Dump Device As... 功能

**优先级**：中  
**复杂度**：低  
**影响范围**：调试/诊断

### 需求描述

File 菜单增加 "Dump Device As..." 功能，将设备文件内容以可读文本形式导出到 .txt 文件。

### 输出格式

```
=== FakeEsptool Device Dump ===
File: C:\path\device.esp
Date: 2026-06-04 14:30:25

[Header]
Magic:      0x45535000 ("ESP\0")
Version:    1
Chip Type:  ESP32
XTAL Freq:  40MHz

[MAC Address]
AA:BB:CC:DD:EE:01

[Flash Config]
Size:       4MB (4,194,304 bytes)

[eFuse] (64 bytes)
Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII
--------  -----------------------  -----------------------  ----------------
00000000  83 1D F0 00 01 EE DD CC  BB CC DD EE 00 00 00 00  ................
00000010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  ................

[Flash Data] (4,194,304 bytes)
Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  ASCII
--------  -----------------------  -----------------------  ----------------
00000000  FF FF FF FF FF FF FF FF  FF FF FF FF FF FF FF FF  ................
...
```

### 实现要点

- 菜单位置：File > Dump Device As...
- 弹出标准文件保存对话框
- 默认文件名：`<设备名>_dump.txt`
- 文件类型过滤：`*.txt` / `*.*`
- 编码：UTF-8 无 BOM
- Flash 数据导出完整内容
- 始终可用

### 参考

- REQUIREMENTS.md §File 菜单
