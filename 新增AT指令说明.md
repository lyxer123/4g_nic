# 新增AT指令说明

## 概述

本次更新新增了以下AT指令功能：

1. **AT+WIFISCAN** - WiFi网络扫描
2. **AT+WIFISTA** - WiFi STA配置（SSID/密码设置与保存）
3. **AT+MODE 增强** - 工作模式切换时自动显示配置提示

---

## 1. AT+WIFISCAN - WiFi网络扫描

### 功能

扫描周围的WiFi接入点（AP），返回可用网络列表，包括SSID、信号强度、加密方式和信道信息。

### 语法

```
AT+WIFISCAN
```

### 响应格式

```
+WIFISCAN:found=<count>
+WIFISCAN:<index>,"<ssid>",<rssi>,<authmode>,<channel>
...
OK
```

### 参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| `count` | 数字 | 找到的AP总数 |
| `index` | 数字 | AP索引（从0开始） |
| `ssid` | 字符串 | WiFi网络名称 |
| `rssi` | 数字 | 信号强度（dBm，负值，越大越好） |
| `authmode` | 字符串 | 加密方式（OPEN/WEP/WPA-PSK/WPA2-PSK等） |
| `channel` | 数字 | 信道号（1-14） |

### 示例

```
AT+WIFISCAN

+WIFISCAN:found=5
+WIFISCAN:0,"MyWiFi", -45,WPA2-PSK,6
+WIFISCAN:1,"Office_Net", -62,WPA/WPA2-PSK,11
+WIFISCAN:2,"CoffeeShop", -78,OPEN,1
+WIFISCAN:3,"Home_5G", -55,WPA2/WPA3-PSK,36
+WIFISCAN:4,"Neighbor", -85,WPA2-PSK,6
OK
```

### 信号强度说明

| RSSI 范围 | 信号质量 | 说明 |
|-----------|----------|------|
| -30 ~ -50 dBm | 优秀 | 非常好的信号 |
| -50 ~ -65 dBm | 良好 | 稳定连接 |
| -65 ~ -75 dBm | 一般 | 可用，可能降速 |
| -75 ~ -90 dBm | 弱 | 不稳定，可能断线 |
| < -90 dBm | 极弱 | 基本无法连接 |

### 加密方式说明

| 加密方式 | 说明 | 安全性 |
|----------|------|--------|
| OPEN | 无加密 | 低 |
| WEP | WEP加密（已淘汰） | 低 |
| WPA-PSK | WPA个人版 | 中 |
| WPA2-PSK | WPA2个人版 | 高 |
| WPA/WPA2-PSK | 混合模式 | 高 |
| WPA2/WPA3-PSK | WPA2/WPA3混合 | 很高 |
| WPA3-SAE | WPA3个人版 | 最高 |

### 注意事项

1. **扫描时间**：扫描约需2-5秒，期间阻塞AT指令
2. **WiFi初始化**：如果WiFi未初始化，会自动初始化
3. **最大数量**：最多返回20个AP
4. **模式影响**：扫描时STA模式必须启动

---

## 2. AT+WIFISTA - WiFi STA配置

### 功能

配置WiFi STA（客户端）模式的连接参数，包括SSID和密码，并保存到NVS持久化存储。

### 语法

#### 查询当前配置

```
AT+WIFISTA?
```

#### 设置STA配置

```
AT+WIFISTA=<ssid>,<password>
```

### 参数说明

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `ssid` | 字符串 | 是 | WiFi网络名称（1-32字符） |
| `password` | 字符串 | 是 | WiFi密码（0-64字符，开放网络可为空） |

### 示例

#### 查询配置

```
AT+WIFISTA?

+WIFISTA:ssid="MyWiFi",password="12345678"
OK
```

未配置时：
```
AT+WIFISTA?

+WIFISTA:not configured
OK
```

#### 设置配置（无引号）

```
AT+WIFISTA=MyWiFi,12345678

+WIFISTA:ok,ssid="MyWiFi"
OK
```

#### 设置配置（带引号，推荐）

```
AT+WIFISTA="MyWiFi","12345678"

+WIFISTA:ok,ssid="MyWiFi"
OK
```

#### 设置开放网络（无密码）

```
AT+WIFISTA="CoffeeShop",""

+WIFISTA:ok,ssid="CoffeeShop"
OK
```

#### SSID包含特殊字符

```
AT+WIFISTA="My WiFi @ Home","p@ssw0rd!"

+WIFISTA:ok,ssid="My WiFi @ Home"
OK
```

### 错误响应

| 错误情况 | 响应 |
|----------|------|
| SSID为空 | ERROR |
| SSID超过32字符 | ERROR |
| 密码超过64字符 | ERROR |
| NVS写入失败 | ERROR |

### 存储位置

配置保存在NVS中：
- **Namespace**: `wifi_cfg`
- **Keys**: `sta_ssid`, `sta_pwd`

### 使用流程

#### 完整配置流程

```
1. 扫描可用网络
   AT+WIFISCAN
   
2. 选择合适的AP
   （从扫描结果中选择）
   
3. 配置STA参数
   AT+WIFISTA="MyWiFi","password123"
   
4. 切换工作模式（如需要）
   AT+MODE=0  （WiFi Router模式）
   
5. 重启设备生效
   AT+RST
```

### 注意事项

1. **密码安全**：密码明文存储在NVS中
2. **重启生效**：配置保存后需要重启或重新连接WiFi
3. **特殊字符**：SSID或密码包含逗号、空格时建议使用引号
4. **长度限制**：SSID最多32字符，密码最多64字符
5. **加密方式**：自动适配AP的加密方式（WPA2/WPA3）

---

## 3. AT+MODE 增强

### 新增功能

设置工作模式后，自动显示该模式所需的配置提示。

### 示例

#### WiFi Router模式（模式0）

```
AT+MODE=0

+MODE:ok work_mode_id=0
+MODE:hint=WiFi STA WAN + SoftAP LAN
+MODE:hint=Use AT+WIFISTA to configure STA
OK
```

#### Ethernet Router模式（模式1）

```
AT+MODE=1

+MODE:ok work_mode_id=1
+MODE:hint=W5500 Ethernet WAN + SoftAP LAN
+MODE:hint=W5500 will auto-configure via DHCP
OK
```

#### USB 4G Router模式（模式2）

```
AT+MODE=2

+MODE:ok work_mode_id=2
+MODE:hint=USB CAT1 4G WAN + SoftAP LAN
+MODE:hint=USB 4G modem will auto-connect
OK
```

#### PPP 4G Router模式（模式3）

```
AT+MODE=3

+MODE:ok work_mode_id=3
+MODE:hint=PPP 4G WAN + SoftAP LAN
+MODE:hint=4G modem will auto-connect via PPP
OK
```

### 工作模式说明

| 模式ID | 名称 | WAN类型 | LAN类型 | 需要配置 |
|--------|------|---------|---------|----------|
| 0 | WiFi Router | WiFi STA | SoftAP | ✅ 需要AT+WIFISTA |
| 1 | Ethernet Router | W5500以太网 | SoftAP | ❌ 自动DHCP |
| 2 | USB 4G Router | USB CAT1 4G | SoftAP | ❌ 自动连接 |
| 3 | PPP 4G Router | PPP 4G | SoftAP | ❌ 自动连接 |

---

## 完整使用示例

### 场景1：配置WiFi Router模式

```bash
# 1. 查看当前模式
AT+MODE?
+MODE:work_mode_id=2
OK

# 2. 扫描可用WiFi
AT+WIFISCAN
+WIFISCAN:found=3
+WIFISCAN:0,"MyHomeWiFi", -42,WPA2-PSK,6
+WIFISCAN:1,"Office", -68,WPA/WPA2-PSK,11
+WIFISCAN:2,"Guest", -80,OPEN,1
OK

# 3. 配置STA连接
AT+WIFISTA="MyHomeWiFi","mySecretPassword"
+WIFISTA:ok,ssid="MyHomeWiFi"
OK

# 4. 切换到WiFi Router模式
AT+MODE=0
+MODE:ok work_mode_id=0
+MODE:hint=WiFi STA WAN + SoftAP LAN
+MODE:hint=Use AT+WIFISTA to configure STA
OK

# 5. 重启生效
AT+RST
OK

# 设备重启后会自动连接 "MyHomeWiFi"
```

### 场景2：切换到以太网模式

```bash
# 1. 切换到以太网模式
AT+MODE=1
+MODE:ok work_mode_id=1
+MODE:hint=W5500 Ethernet WAN + SoftAP LAN
+MODE:hint=W5500 will auto-configure via DHCP
OK

# 2. 重启生效
AT+RST
OK

# 设备重启后W5500自动通过DHCP获取IP
```

### 场景3：查询和修改STA配置

```bash
# 查询当前STA配置
AT+WIFISTA?
+WIFISTA:ssid="MyHomeWiFi",password="mySecretPassword"
OK

# 修改STA配置
AT+WIFISTA="NewNetwork","newPassword123"
+WIFISTA:ok,ssid="NewNetwork"
OK

# 验证配置已保存
AT+WIFISTA?
+WIFISTA:ssid="NewNetwork",password="newPassword123"
OK
```

---

## 错误处理

### 常见错误

| 错误 | 原因 | 解决方法 |
|------|------|----------|
| `ERROR` (WIFISCAN) | WiFi未初始化或扫描失败 | 检查WiFi硬件，重试 |
| `ERROR` (WIFISTA) | SSID为空 | 提供有效的SSID |
| `ERROR` (WIFISTA) | SSID过长 | SSID不超过32字符 |
| `ERROR` (WIFISTA) | 密码过长 | 密码不超过64字符 |
| `ERROR` (WIFISTA) | NVS空间不足 | 擦除NVS或释放空间 |

### 调试技巧

```bash
# 1. 检查WiFi状态
AT+ROUTER

# 2. 检查系统内存
AT+MEM

# 3. 查看详细错误日志
# （通过串口监视器查看ESP_LOG输出）
```

---

## 技术实现

### 依赖的ESP-IDF API

- `esp_wifi_scan_start()` - 启动WiFi扫描
- `esp_wifi_scan_get_ap_records()` - 获取扫描结果
- `esp_wifi_set_config()` - 设置WiFi配置
- `nvs_set_str()` / `nvs_get_str()` - NVS存储

### NVS存储结构

```
NVS
└── wifi_cfg (namespace)
    ├── sta_ssid (string) - STA SSID
    └── sta_pwd (string)  - STA Password
```

### 代码位置

- **实现文件**: `components/router_at/router_at.c`
- **头文件**: `components/router_at/include/router_at.h`
- **相关函数**:
  - `at_handle_query()` - AT指令处理主函数
  - `save_sta_to_nvs()` - 保存STA配置（web_service.c）

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-04-17 | 初始版本，新增AT+WIFISCAN和AT+WIFISTA |

---

## 相关文档

- [AT指令说明.md](../../at指令说明.md) - 完整AT指令列表
- [components/router_at/README.md](../router_at/README.md) - Router AT组件文档
- [PCSoftware/AT指令说明.md](../../PCSoftware/AT指令说明.md) - PC软件AT测试指南
