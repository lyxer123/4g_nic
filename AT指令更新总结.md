# AT指令更新总结

## 更新日期
2026-04-17

## 更新内容

### 1. 新增 AT+WIFISCAN 指令

**功能：** 扫描周围WiFi网络，返回AP列表

**实现位置：** `components/router_at/router_at.c` 第589-650行

**主要特性：**
- 最多扫描20个AP
- 返回SSID、信号强度(RSSI)、加密方式、信道
- 自动初始化WiFi（如果未初始化）
- 阻塞式扫描（2-5秒）

**代码关键部分：**
```c
// WiFi扫描
wifi_ap_record_t ap_records[20];
uint16_t ap_count = 20;
esp_wifi_scan_start(NULL, true);  // 阻塞扫描
esp_wifi_scan_get_ap_records(&ap_count, ap_records);
```

---

### 2. 新增 AT+WIFISTA 指令

**功能：** 配置WiFi STA模式的SSID和密码，并保存到NVS

**实现位置：** `components/router_at/router_at.c` 第652-785行

**主要特性：**
- 支持查询模式：`AT+WIFISTA?`
- 支持设置模式：`AT+WIFISTA=<ssid>,<password>`
- 支持带引号和不带引号的参数格式
- 参数验证（SSID长度、密码长度）
- NVS持久化存储（namespace: `wifi_cfg`）

**代码关键部分：**
```c
// NVS保存
nvs_open("wifi_cfg", NVS_READWRITE, &nvs);
nvs_set_str(nvs, "sta_ssid", ssid);
nvs_set_str(nvs, "sta_pwd", password);
nvs_commit(nvs);
```

**解析逻辑：**
- 支持 `AT+WIFISTA=MyWiFi,password`
- 支持 `AT+WIFISTA="MyWiFi","password"`
- 正确处理特殊字符（逗号、空格等）

---

### 3. 增强 AT+MODE 指令

**功能：** 设置工作模式时自动显示配置提示

**实现位置：** `components/router_at/router_at.c` 第382-413行

**主要特性：**
- 模式0（WiFi Router）：提示使用AT+WIFISTA配置STA
- 模式1（Ethernet Router）：提示W5500自动DHCP
- 模式2（USB 4G Router）：提示USB 4G自动连接
- 模式3（PPP 4G Router）：提示PPP自动连接

**代码关键部分：**
```c
// 模式切换后显示提示
switch ((uint8_t)v) {
    case 0:  // WiFi Router Mode
        at_write(write_bytes, "+MODE:hint=WiFi STA WAN + SoftAP LAN\r\n");
        at_write(write_bytes, "+MODE:hint=Use AT+WIFISTA to configure STA\r\n");
        break;
    case 1:  // Ethernet Router Mode
        at_write(write_bytes, "+MODE:hint=W5500 Ethernet WAN + SoftAP LAN\r\n");
        break;
    // ...
}
```

---

## 修改文件清单

| 文件 | 修改内容 | 行数变化 |
|------|----------|----------|
| `components/router_at/router_at.c` | 添加WiFi头文件、新增指令实现 | +209行 |
| `components/router_at/README.md` | 更新指令列表和功能说明 | +9行 |
| `新增AT指令说明.md` | 新创建，完整使用说明 | +424行 |
| `AT指令更新总结.md` | 本文件，技术总结 | - |

---

## 依赖关系

### 新增头文件

```c
#include "esp_wifi.h"          // WiFi扫描和配置
#include "freertos/event_groups.h"  // WiFi事件（预留）
```

### NVS存储

| Namespace | Key | 类型 | 说明 |
|-----------|-----|------|------|
| `wifi_cfg` | `sta_ssid` | string | STA SSID |
| `wifi_cfg` | `sta_pwd` | string | STA密码 |

---

## 测试用例

### 测试1：WiFi扫描

```bash
AT+WIFISCAN
# 预期：返回AP列表
```

### 测试2：STA配置查询（未配置）

```bash
AT+WIFISTA?
# 预期：+WIFISTA:not configured
```

### 测试3：STA配置设置

```bash
AT+WIFISTA="TestWiFi","12345678"
# 预期：+WIFISTA:ok,ssid="TestWiFi"
```

### 测试4：STA配置查询（已配置）

```bash
AT+WIFISTA?
# 预期：+WIFISTA:ssid="TestWiFi",password="12345678"
```

### 测试5：模式切换提示

```bash
AT+MODE=0
# 预期：显示WiFi Router模式提示，建议使用AT+WIFISTA
```

---

## 工作流程

### WiFi Router模式配置流程

```
1. AT+WIFISCAN          # 扫描可用网络
   ↓
2. 选择合适的AP         # 从扫描结果选择
   ↓
3. AT+WIFISTA="SSID","PWD"  # 配置STA参数
   ↓
4. AT+MODE=0            # 切换到WiFi Router模式
   ↓
5. AT+RST               # 重启生效
   ↓
6. 设备自动连接WiFi     # 从NVS读取配置并连接
```

---

## 注意事项

### 1. WiFi扫描注意事项
- 扫描是阻塞操作，耗时2-5秒
- 扫描期间其他AT指令会等待
- 最多返回20个AP
- 需要WiFi已初始化

### 2. STA配置注意事项
- SSID长度：1-32字符
- 密码长度：0-64字符
- 密码明文存储在NVS
- 需要重启或重新连接才能生效
- 特殊字符建议使用引号包裹

### 3. 模式切换注意事项
- 模式切换后需要重启生效
- 不同模式需要不同的硬件支持
- 模式0需要预先配置STA参数
- 其他模式通常自动配置

### 4. NVS存储注意事项
- NVS空间有限，注意清理
- 写入失败会返回ERROR
- 配置持久化，重启不丢失

---

## 错误处理

### WiFi扫描错误

| 错误 | 原因 | 处理 |
|------|------|------|
| ESP_ERR_WIFI_NOT_INIT | WiFi未初始化 | 自动初始化 |
| ESP_ERR_WIFI_CONN | 连接状态异常 | 返回ERROR |
| ESP_ERR_NO_MEM | 内存不足 | 返回ERROR |

### STA配置错误

| 错误 | 原因 | 处理 |
|------|------|------|
| SSID为空 | 参数错误 | 返回ERROR |
| SSID>32字符 | 超长 | 返回ERROR |
| 密码>64字符 | 超长 | 返回ERROR |
| NVS写入失败 | 空间不足 | 返回ERROR |

---

## 性能影响

### 内存占用
- WiFi扫描缓冲区：约2KB（20个AP记录）
- NVS存储：约128字节（SSID+密码）

### 时间消耗
- WiFi扫描：2-5秒（阻塞）
- NVS写入：<100ms
- 模式切换：立即（重启后生效）

---

## 后续扩展建议

### 1. 增加AT+WIFICONNECT指令
- 立即连接指定的WiFi
- 不需要重启

### 2. 增加AT+WIFISTATUS指令
- 查询当前WiFi连接状态
- 显示IP地址、信号强度等

### 3. 增加AT+WIFIDISCONNECT指令
- 断开当前WiFi连接
- 清除STA配置

### 4. 支持WPS配置
- AT+WIFIWPS启动WPS配网
- 简化配置流程

---

## 相关文档

- [新增AT指令说明.md](./新增AT指令说明.md) - 详细使用说明
- [components/router_at/README.md](./components/router_at/README.md) - 组件文档
- [at指令说明.md](./at指令说明.md) - 完整AT指令列表

---

## 版本信息

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-04-17 | 初始版本，新增WiFi相关指令 |
