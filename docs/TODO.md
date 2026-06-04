# FakeEsptool - 待办改进项

本文档记录已识别但尚未实现的功能增强和改进项。

---

## 1. 命令状态机

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
