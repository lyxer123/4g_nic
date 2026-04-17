# AT指令拆分说明

## 更新日期
2026-04-17

## 变更内容

将原来的 `AT+MODE` 指令拆分为两个独立指令：

1. **AT+MODE?** - 查询当前工作模式
2. **AT+MODESET=<id>** - 设置并保存工作模式

---

## 1. AT+MODE? - 查询工作模式

### 功能

查询设备当前配置的工作模式ID。

### 语法

```
AT+MODE?
```

或（兼容旧格式）：
```
AT+MODE
```

### 响应格式

```
+MODE?:work_mode_id=<id>
OK
```

### 示例

```
AT+MODE?

+MODE?:work_mode_id=2
OK
```

### 工作模式ID说明

| 模式ID | 名称 | WAN类型 | LAN类型 |
|--------|------|---------|---------|
| 0 | WiFi Router | WiFi STA | SoftAP |
| 1 | Ethernet Router | W5500以太网 | SoftAP |
| 2 | USB 4G Router | USB CAT1 4G | SoftAP |
| 3 | PPP 4G Router | PPP 4G | SoftAP |

---

## 2. AT+MODESET=<id> - 设置工作模式

### 功能

设置并保存设备的工作模式，切换后需要重启生效。

### 语法

```
AT+MODESET=<mode_id>
```

### 参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `mode_id` | 数字 | 是 | 工作模式ID（0-3） |

### 响应格式

```
+MODESET:ok work_mode_id=<id>
+MODESET:hint=<提示1>
+MODESET:hint=<提示2>
OK
```

### 示例

#### 设置WiFi Router模式

```
AT+MODESET=0

+MODESET:ok work_mode_id=0
+MODESET:hint=WiFi STA WAN + SoftAP LAN
+MODESET:hint=Use AT+WIFISTA to configure STA
OK
```

#### 设置以太网模式

```
AT+MODESET=1

+MODESET:ok work_mode_id=1
+MODESET:hint=W5500 Ethernet WAN + SoftAP LAN
+MODESET:hint=W5500 will auto-configure via DHCP
OK
```

#### 设置USB 4G模式

```
AT+MODESET=2

+MODESET:ok work_mode_id=2
+MODESET:hint=USB CAT1 4G WAN + SoftAP LAN
+MODESET:hint=USB 4G modem will auto-connect
OK
```

#### 设置PPP 4G模式

```
AT+MODESET=3

+MODESET:ok work_mode_id=3
+MODESET:hint=PPP 4G WAN + SoftAP LAN
+MODESET:hint=4G modem will auto-connect via PPP
OK
```

### 错误响应

| 错误情况 | 响应 |
|----------|------|
| 缺少参数 | ERROR |
| 参数无效 | ERROR |
| 模式ID超出范围 | ERROR |
| NVS写入失败 | ERROR |

---

## 对比变化

### 旧版（AT+MODE）

```
# 查询
AT+MODE?
+MODE:work_mode_id=2
OK

# 设置
AT+MODE=0
+MODE:ok work_mode_id=0
+MODE:hint=...
OK
```

### 新版（拆分后）

```
# 查询
AT+MODE?
+MODE?:work_mode_id=2
OK

# 设置
AT+MODESET=0
+MODESET:ok work_mode_id=0
+MODESET:hint=...
OK
```

---

## 使用场景

### 场景1：查看当前模式

```
AT+MODE?
+MODE?:work_mode_id=2
OK
```

### 场景2：切换模式

```
# 1. 查看当前模式
AT+MODE?
+MODE?:work_mode_id=2
OK

# 2. 切换到WiFi Router模式
AT+MODESET=0
+MODESET:ok work_mode_id=0
+MODESET:hint=WiFi STA WAN + SoftAP LAN
+MODESET:hint=Use AT+WIFISTA to configure STA
OK

# 3. 重启生效
AT+RST
```

### 场景3：配置完整流程

```
# 1. 查询当前模式
AT+MODE?

# 2. 扫描WiFi
AT+WIFISCAN

# 3. 配置STA
AT+WIFISTA="MyWiFi","password123"

# 4. 切换到WiFi Router模式
AT+MODESET=0

# 5. 重启
AT+RST
```

---

## 兼容性说明

### 向后兼容

- `AT+MODE` (不带参数) 仍可用作查询，等同于 `AT+MODE?`
- 返回格式从 `+MODE:` 改为 `+MODE?:`

### 新格式要求

- 设置模式必须使用 `AT+MODESET=<id>`
- 旧的 `AT+MODE=<id>` 格式不再支持

---

## 优势

### 1. 语义更清晰

- **AT+MODE?** - 明确是查询操作
- **AT+MODESET** - 明确是设置操作

### 2. 符合AT指令规范

标准AT指令格式：
- `AT+CMD?` - 查询
- `AT+CMD=<params>` - 设置
- `AT+CMD=?` - 测试支持的值

### 3. 避免混淆

- 查询和设置分开，不易误操作
- 返回值前缀不同（`+MODE?:` vs `+MODESET:`）

---

## 代码实现

### AT+MODE? 实现

```c
if (strcmp(name, "MODE?") == 0 || strcmp(name, "MODE") == 0) {
    /* AT+MODE? - Query work mode */
    uint8_t m = 0;
    esp_err_t e = web_service_get_work_mode_u8(&m);
    if (e != ESP_OK) {
        at_error(write_bytes);
        return;
    }
    at_write_fmt(write_bytes, "\r\n+MODE?:work_mode_id=%u\r\n", (unsigned)m);
    at_ok(write_bytes);
    return;
}
```

### AT+MODESET 实现

```c
if (strcmp(name, "MODESET") == 0) {
    /* AT+MODESET=<id> - Set and save work mode */
    // 解析参数
    // 验证模式ID
    // 保存到NVS
    // 显示配置提示
    // 返回成功
}
```

---

## 测试用例

### 测试1：查询模式

```bash
AT+MODE?
# 预期：+MODE?:work_mode_id=X
```

### 测试2：设置模式

```bash
AT+MODESET=0
# 预期：+MODESET:ok work_mode_id=0 + 提示信息
```

### 测试3：设置无效模式

```bash
AT+MODESET=99
# 预期：ERROR
```

### 测试4：缺少参数

```bash
AT+MODESET
# 预期：ERROR
```

### 测试5：兼容性测试

```bash
AT+MODE
# 预期：等同于AT+MODE?
```

---

## 相关文件

| 文件 | 修改内容 |
|------|----------|
| `components/router_at/router_at.c` | 拆分AT+MODE为两个指令 |
| `components/router_at/README.md` | 更新指令列表 |
| `新增AT指令说明.md` | 更新指令说明 |

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-04-17 | 初始版本，AT+MODE为单一指令 |
| 1.1.0 | 2026-04-17 | 拆分为AT+MODE?和AT+MODESET |
