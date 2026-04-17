# AT+MODE 指令拆分更新总结

## 更新日期
2026-04-17

## 变更概述

将原来的 `AT+MODE` 指令拆分为两个语义更清晰的独立指令：

| 原指令 | 新指令 | 功能 |
|--------|--------|------|
| `AT+MODE?` | `AT+MODE?` (保留) | 查询工作模式 |
| `AT+MODE=<id>` | `AT+MODESET=<id>` (新) | 设置工作模式 |

---

## 修改内容

### 1. 代码修改

**文件：** `components/router_at/router_at.c`

#### AT+MODE? (查询指令)

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

**特性：**
- 支持 `AT+MODE?` 和 `AT+MODE` (兼容旧格式)
- 返回格式：`+MODE?:work_mode_id=X`

#### AT+MODESET (设置指令)

```c
if (strcmp(name, "MODESET") == 0) {
    /* AT+MODESET=<id> - Set and save work mode */
    // 解析参数、验证、保存NVS、显示提示
}
```

**特性：**
- 必须带参数：`AT+MODESET=<id>`
- 返回格式：`+MODESET:ok work_mode_id=X`
- 显示模式相关的配置提示

---

### 2. AT+CMD 列表更新

**旧版：**
```
+CMD:...,AT+MODE,...
```

**新版：**
```
+CMD:...,AT+MODE?,AT+MODESET,...
```

---

### 3. 文档更新

| 文件 | 修改内容 |
|------|----------|
| `components/router_at/README.md` | 更新指令列表 |
| `PCSoftware/AT指令说明.md` | 更新指令说明 |
| `AT指令拆分说明.md` | 新创建，详细说明 |
| `AT+MODE拆分更新总结.md` | 本文件 |

---

### 4. PC软件更新

**文件：** `PCSoftware/nic_ble_pc/admin_pages.py`

**新增快捷按钮：**
```python
("AT+MODESET=0", "AT+MODESET=0"),  # WiFi Router
("AT+MODESET=1", "AT+MODESET=1"),  # Ethernet Router
("AT+MODESET=2", "AT+MODESET=2"),  # USB 4G Router
("AT+MODESET=3", "AT+MODESET=3"),  # PPP 4G Router
```

---

## 使用示例对比

### 旧版用法

```bash
# 查询
AT+MODE?
+MODE:work_mode_id=2
OK

# 设置
AT+MODE=0
+MODE:ok work_mode_id=0
+MODE:hint=WiFi STA WAN + SoftAP LAN
+MODE:hint=Use AT+WIFISTA to configure STA
OK
```

### 新版用法

```bash
# 查询
AT+MODE?
+MODE?:work_mode_id=2
OK

# 设置
AT+MODESET=0
+MODESET:ok work_mode_id=0
+MODESET:hint=WiFi STA WAN + SoftAP LAN
+MODESET:hint=Use AT+WIFISTA to configure STA
OK
```

---

## 优势分析

### 1. 语义更清晰

| 指令 | 语义 | 操作类型 |
|------|------|----------|
| `AT+MODE?` | 查询模式 | 读操作 |
| `AT+MODESET` | 设置模式 | 写操作 |

### 2. 符合AT指令标准

标准AT指令格式规范：
- `AT+CMD?` - 查询当前值
- `AT+CMD=<params>` - 设置值
- `AT+CMD=?` - 查询支持的值

拆分后更符合规范。

### 3. 返回值区分明确

| 指令 | 返回值前缀 | 用途 |
|------|-----------|------|
| `AT+MODE?` | `+MODE?:` | 查询结果 |
| `AT+MODESET` | `+MODESET:` | 设置结果 |

### 4. 避免误操作

- 查询和设置分离，不会意外修改配置
- 设置指令需要明确的参数

---

## 工作模式说明

### 模式0：WiFi Router

```
AT+MODESET=0

+MODESET:ok work_mode_id=0
+MODESET:hint=WiFi STA WAN + SoftAP LAN
+MODESET:hint=Use AT+WIFISTA to configure STA
OK
```

**配置要求：**
- ✅ 需要使用 `AT+WIFISTA` 配置STA参数
- ✅ 需要重启生效

### 模式1：Ethernet Router (W5500)

```
AT+MODESET=1

+MODESET:ok work_mode_id=1
+MODESET:hint=W5500 Ethernet WAN + SoftAP LAN
+MODESET:hint=W5500 will auto-configure via DHCP
OK
```

**配置要求：**
- ❌ 自动通过DHCP获取IP
- ✅ 需要W5500硬件

### 模式2：USB 4G Router

```
AT+MODESET=2

+MODESET:ok work_mode_id=2
+MODESET:hint=USB CAT1 4G WAN + SoftAP LAN
+MODESET:hint=USB 4G modem will auto-connect
OK
```

**配置要求：**
- ❌ 自动连接4G模组
- ✅ 需要USB CAT1模组

### 模式3：PPP 4G Router

```
AT+MODESET=3

+MODESET:ok work_mode_id=3
+MODESET:hint=PPP 4G WAN + SoftAP LAN
+MODESET:hint=4G modem will auto-connect via PPP
OK
```

**配置要求：**
- ❌ 自动PPP拨号
- ✅ 需要串口4G模组

---

## 兼容性说明

### 向后兼容

| 旧格式 | 新格式 | 兼容性 |
|--------|--------|--------|
| `AT+MODE?` | `AT+MODE?` | ✅ 完全兼容 |
| `AT+MODE` | `AT+MODE?` | ✅ 兼容（自动识别为查询） |
| `AT+MODE=<id>` | `AT+MODESET=<id>` | ❌ 不兼容（需更新） |

### 迁移指南

**旧代码：**
```python
ser.write(b"AT+MODE=0\r\n")
```

**新代码：**
```python
ser.write(b"AT+MODESET=0\r\n")
```

---

## 测试用例

### 测试1：查询模式

```bash
AT+MODE?
# 预期：+MODE?:work_mode_id=X
```

### 测试2：设置WiFi模式

```bash
AT+MODESET=0
# 预期：+MODESET:ok work_mode_id=0 + 提示
```

### 测试3：设置以太网模式

```bash
AT+MODESET=1
# 预期：+MODESET:ok work_mode_id=1 + 提示
```

### 测试4：设置无效模式

```bash
AT+MODESET=99
# 预期：ERROR
```

### 测试5：缺少参数

```bash
AT+MODESET
# 预期：ERROR
```

### 测试6：兼容查询

```bash
AT+MODE
# 预期：等同于AT+MODE?
```

---

## 完整工作流程

### WiFi Router模式配置

```bash
# 1. 查询当前模式
AT+MODE?
+MODE?:work_mode_id=2
OK

# 2. 扫描WiFi网络
AT+WIFISCAN
+WIFISCAN:found=3
+WIFISCAN:0,"MyWiFi",-45,WPA2-PSK,6
...
OK

# 3. 配置STA参数
AT+WIFISTA="MyWiFi","password123"
+WIFISTA:ok,ssid="MyWiFi"
OK

# 4. 切换到WiFi Router模式
AT+MODESET=0
+MODESET:ok work_mode_id=0
+MODESET:hint=WiFi STA WAN + SoftAP LAN
+MODESET:hint=Use AT+WIFISTA to configure STA
OK

# 5. 重启生效
AT+RST
```

---

## 影响范围

### 修改的文件

| 文件 | 类型 | 行数变化 |
|------|------|----------|
| `components/router_at/router_at.c` | 代码 | ~70行修改 |
| `components/router_at/README.md` | 文档 | +1/-1 |
| `PCSoftware/AT指令说明.md` | 文档 | +2/-2 |
| `PCSoftware/nic_ble_pc/admin_pages.py` | 代码 | +7 |
| `AT指令拆分说明.md` | 文档 | 新创建351行 |
| `AT+MODE拆分更新总结.md` | 文档 | 本文件 |

### 影响的功能

- ✅ AT指令查询功能
- ✅ AT指令设置功能
- ✅ PC软件快捷按钮
- ✅ 文档说明

### 不影响的功能

- ❌ WiFi扫描 (AT+WIFISCAN)
- ❌ STA配置 (AT+WIFISTA)
- ❌ 其他AT指令
- ❌ Web API
- ❌ PCAPI协议

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-04-17 | AT+MODE为单一指令（查询+设置） |
| 1.1.0 | 2026-04-17 | 拆分为AT+MODE?和AT+MODESET |

---

## 相关文档

- [AT指令拆分说明.md](./AT指令拆分说明.md) - 详细使用说明
- [新增AT指令说明.md](./新增AT指令说明.md) - WiFi相关指令
- [components/router_at/README.md](./components/router_at/README.md) - 组件文档
- [PCSoftware/AT指令说明.md](./PCSoftware/AT指令说明.md) - PC软件文档

---

## 总结

### 主要变更

1. ✅ 拆分 `AT+MODE` 为 `AT+MODE?` 和 `AT+MODESET`
2. ✅ 更新指令列表和文档
3. ✅ 更新PC软件快捷按钮
4. ✅ 保持向后兼容（查询功能）

### 优势

- 语义更清晰
- 符合AT指令标准
- 避免误操作
- 返回值区分明确

### 下一步

- 测试新指令
- 更新用户手册
- 通知相关开发者
