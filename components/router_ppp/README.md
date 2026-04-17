# Router PPP 组件

## 功能概述

Router PPP 组件实现了 Point-to-Point Protocol (PPP) 网络接口，用于通过串口与外部 PPP 设备（如 4G 模组）建立网络连接。该组件基于 ESP-IDF 的 PPPoS (PPP over Serial) 实现，为设备提供 WAN 网络接入能力。

### 主要功能

- PPP over Serial (PPPoS) 网络接口
- 4G 模组拨号上网
- 自动重连机制
- PPP 网络接口管理
- 状态监控

## 架构说明

### PPP 连接流程

```
┌─────────────┐     UART      ┌──────────────┐
│  ESP32-S3   │ ◄───────────► │  4G Modem    │
│             │    PPPoS       │              │
│  PPP Netif  │                │  PPP Server  │
└──────┬──────┘                └──────────────┘
       │
       ▼
┌─────────────┐
│  NAPT/WAN   │
│  Routing    │
└─────────────┘
```

## 快速开始

### 初始化

```c
#include "router_ppp.h"

void app_main(void)
{
    // 网络接口初始化
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 启动 PPP 连接
    router_ppp_start();
    
    // 检查 PPP 状态
    if (router_ppp_is_running()) {
        ESP_LOGI(TAG, "PPP connection started");
    }
    
    // 获取 PPP 网络接口
    esp_netif_t *ppp_netif = router_ppp_get_netif();
    if (ppp_netif) {
        ESP_LOGI(TAG, "PPP netif available");
    }
}
```

### API 接口

#### `router_ppp_start()`

启动 PPP 连接。

**函数原型：**
```c
void router_ppp_start(void);
```

**说明：**
- 创建 PPP 网络接口
- 配置 UART 与模组通信
- 启动 PPP 拨号流程
- 自动注册事件处理器

**阻塞特性：**
- 非阻塞：立即返回
- 后台任务处理 PPP 连接

#### `router_ppp_is_running()`

检查 PPP 是否正在运行。

**函数原型：**
```c
bool router_ppp_is_running(void);
```

**返回值：**
- `true`: PPP 连接已建立并运行
- `false`: PPP 未运行

**使用场景：**
```c
if (router_ppp_is_running()) {
    ESP_LOGI(TAG, "PPP is active");
} else {
    ESP_LOGW(TAG, "PPP not running");
}
```

#### `router_ppp_get_netif()`

获取 PPP 网络接口指针。

**函数原型：**
```c
esp_netif_t *router_ppp_get_netif(void);
```

**返回值：**
- `esp_netif_t*`: PPP 网络接口指针
- `NULL`: PPP 未初始化

**使用场景：**
```c
esp_netif_t *ppp_netif = router_ppp_get_netif();
if (ppp_netif) {
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ppp_netif, &ip_info);
    ESP_LOGI(TAG, "PPP IP: " IPSTR, IP2STR(&ip_info.ip));
}
```

## 依赖关系

### 硬件依赖

- UART 端口（与 4G 模组通信）
- 4G 模组（支持 PPP 拨号）
- SIM 卡（已激活）

### ESP-IDF 组件依赖

- `esp_netif` (网络接口)
- `esp_event` (事件处理)
- `driver` (UART 驱动)
- `ppp_support` (PPP 支持)
- `esp_modem` (模组控制)

### 项目组件依赖

- `system` (系统管理)
- `iot_bridge` (桥接框架，可选)

### CMake 配置

```cmake
idf_component_register(
    SRCS "router_ppp.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_event driver ppp_support
    PRIV_REQUIRES system
)
```

## 配置选项

### menuconfig 配置

```
Component config --->
    Router PPP Configuration --->
        [*] Enable PPP over UART
        PPP UART Port (UART1) --->
        PPP Baud Rate (115200) --->
        APN Configuration --->
            (cmnet) APN Name
```

### SDK Config 示例

```kconfig
CONFIG_ROUTER_PPP_ENABLE=y
CONFIG_ROUTER_PPP_UART_NUM=1
CONFIG_ROUTER_PPP_BAUD_RATE=115200
CONFIG_ROUTER_PPP_APN="cmnet"
```

## PPP 连接状态

### 状态机

```
IDLE → INITIALIZING → CONNECTING → CONNECTED
                              ↓
                          DISCONNECTED → RECONNECTING
```

### 事件处理

组件自动处理以下 PPP 事件：

| 事件 | 说明 | 处理 |
|------|------|------|
| `IP_EVENT_PPP_GOT_IP` | 获取到 IP 地址 | 记录日志，更新状态 |
| `IP_EVENT_PPP_LOST_IP` | 失去 IP 连接 | 标记断开，准备重连 |
| `MODEM_EVENT_PPP_CONNECTED` | PPP 链路建立 | 更新连接状态 |
| `MODEM_EVENT_PPP_DISCONNECTED` | PPP 链路断开 | 触发重连 |

## 使用场景

### 1. 4G WAN 连接

```c
// 启动 PPP 拨号
router_ppp_start();

// 等待连接建立（通过事件）
// 使用 router_ppp_get_netif() 获取网络接口
// 配置 NAPT 转发
```

### 2. 网络接口管理

```c
esp_netif_t *ppp_netif = router_ppp_get_netif();

// 获取 IP 信息
esp_netif_ip_info_t ip_info;
if (esp_netif_get_ip_info(ppp_netif, &ip_info) == ESP_OK) {
    ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&ip_info.gw));
    ESP_LOGI(TAG, "NM: " IPSTR, IP2STR(&ip_info.netmask));
}
```

### 3. 连接状态监控

```c
while (1) {
    if (router_ppp_is_running()) {
        ESP_LOGI(TAG, "PPP connected");
    } else {
        ESP_LOGW(TAG, "PPP disconnected, retrying...");
        router_ppp_start();
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
}
```

## 网络拓扑

### 典型应用

```
Internet
   ↑
4G Base Station
   ↑ (LTE)
4G Modem
   ↑ (UART/PPP)
ESP32-S3 (Router)
   ↑ (SoftAP/WiFi)
Client Devices
```

### 多 WAN 模式

```
              ┌→ WiFi WAN
Internet ←───┼→ W5500 Ethernet
              └→ 4G PPP ← 本组件
```

## 注意事项

### 1. UART 配置

- 确保 UART 端口未被其他组件占用
- 波特率需与 4G 模组匹配
- 建议使用硬件流控制（RTS/CTS）

### 2. 模组初始化

- PPP 拨号前需确保模组已就绪
- SIM 卡已插入并激活
- 信号强度足够

### 3. APN 设置

- 不同运营商 APN 不同
- 常见 APN：
  - 中国移动：`cmnet`
  - 中国联通：`3gnet`
  - 中国电信：`ctnet`

### 4. 重连机制

- PPP 断开后自动重连
- 重连间隔可配置
- 避免频繁重连导致模组异常

### 5. 内存占用

- PPP 协议栈占用约 20KB RAM
- 网络缓冲区额外占用
- 确保有足够 heap 空间

## 故障排查

### 问题：PPP 无法连接

**可能原因：**
1. UART 通信失败
2. 模组未就绪
3. SIM 卡异常
4. APN 配置错误
5. 信号弱

**排查步骤：**
```c
// 1. 检查模组状态
AT+MODEMINFO

// 2. 检查 UART
ESP_LOGI(TAG, "PPP UART: %d", CONFIG_ROUTER_PPP_UART_NUM);

// 3. 检查信号
AT+CSQ

// 4. 手动拨号测试
ATD*99#
```

### 问题：频繁断线重连

**可能原因：**
1. 信号不稳定
2. 电源不足
3. 天线接触不良
4. SIM 卡接触不良

**解决方法：**
```c
// 监控连接状态
if (!router_ppp_is_running()) {
    ESP_LOGW(TAG, "PPP disconnected");
    // 延迟后重连
    vTaskDelay(pdMS_TO_TICKS(3000));
    router_ppp_start();
}
```

### 问题：获取到 IP 但无法上网

**检查项：**
1. 默认路由是否正确
2. NAPT 是否启用
3. DNS 服务器配置
4. 防火墙规则

## 与其他 WAN 组件配合

### WiFi WAN

```c
// 优先使用 WiFi WAN
// WiFi 断开时切换到 PPP
if (!wifi_wan_connected()) {
    router_ppp_start();
}
```

### 以太网 WAN

```c
// 多 WAN 负载均衡
// PPP 作为备用链路
if (!eth_wan_connected()) {
    router_ppp_start();
}
```

## 性能优化

### 1. 缓冲区配置

```kconfig
CONFIG_PPP_OUR_NETIF_MTU=1500
CONFIG_PPP_RX_PBUF_TCOUNT=16
CONFIG_PPP_TX_PBUF_TCOUNT=16
```

### 2. UART 优化

- 增加 UART 缓冲区大小
- 启用 DMA（如果支持）
- 调整中断优先级

### 3. PPP 参数

- 调整 LCP 心跳间隔
- 配置 IPCP 协商参数
- 启用压缩（如果模组支持）

## 扩展功能

### 自定义 APN

```c
// 通过 NVS 动态配置 APN
nvs_handle_t handle;
nvs_open("router_ppp", NVS_READWRITE, &handle);
char apn[64];
size_t len = sizeof(apn);
nvs_get_str(handle, "apn", apn, &len);
// 使用自定义 APN 拨号
```

### 多 SIM 支持

```c
// 切换 SIM 卡后重新拨号
switch_sim_card();
vTaskDelay(pdMS_TO_TICKS(2000));
router_ppp_start();
```

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2024 | 初始版本 |

## 相关文件

```
components/router_ppp/
├── CMakeLists.txt          # CMake 构建配置
├── Kconfig                 # menuconfig 配置
├── router_ppp.c            # 主实现文件
└── include/
    └── router_ppp.h        # 公共头文件
```

## 相关文档

- [多wan智能设计.md](../../多wan智能设计.md) - 多 WAN 架构设计
- [路由器解决方案.md](../../路由器解决方案.md) - 整体路由器方案

## 许可证

Apache-2.0
